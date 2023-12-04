/*
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following
   disclaimer in the documentation and/or other materials provided
   with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived
   from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


This code implements the shortest augmenting path algorithm for the
rectangular assignment problem.  This implementation is based on the
pseudocode described in pages 1685-1686 of:

    DF Crouse. On implementing 2D rectangular assignment algorithms.
    IEEE Transactions on Aerospace and Electronic Systems
    52(4):1679-1696, August 2016
    doi: 10.1109/TAES.2016.140952

Author: PM Larsen
        Jakub Galgonek
*/

#include <postgres.h>
#include <math.h>
#include <stdbool.h>
#include "similarity/lsap.h"


static inline int augmenting_path(int nr, int nc, const float *restrict cost, float offset, const float *restrict u,
        const float *restrict v, int *restrict path, int *restrict row4col, float *restrict shortest_paths, int i,
        bool *restrict sr, bool *restrict sc, int *restrict remaining, float *restrict pmin)
{
    float min = 0;
    int num_remaining = nc;

    // Filling this up in reverse order ensures that the solution of a
    // constant cost matrix is the identity matrix (c.f. #11602).
    for(int it = 0; it < nc; it++)
        remaining[it] = nc - it - 1;

    for(int i = 0; i < nr; i++)
        sr[i] = false;

    for(int i = 0; i < nc; i++)
        sc[i] = false;

    for(int i = 0; i < nc; i++)
        shortest_paths[i] = INFINITY;


    // find shortest augmenting path
    int sink = -1;
    while(sink == -1)
    {
        int index = -1;
        float lowest = INFINITY;
        sr[i] = true;

        for(int it = 0; it < num_remaining; it++)
        {
            int j = remaining[it];

            // true cost value is offset - cost[i * nc + j]
            float r = min + offset - cost[i * (size_t) nc + j] - u[i] - v[j];

            if(r < shortest_paths[j])
            {
                path[j] = i;
                shortest_paths[j] = r;
            }

            // When multiple nodes have the minimum cost, we select one which
            // gives us a new sink node. This is particularly important for
            // integer cost matrices with small co-efficients.
            if(shortest_paths[j] < lowest || (shortest_paths[j] == lowest && row4col[j] == -1))
            {
                lowest = shortest_paths[j];
                index = it;
            }
        }

        min = lowest;
        int j = remaining[index];

        if(min == INFINITY)
            return -1; // infeasible cost matrix

        if(row4col[j] == -1)
            sink = j;
        else
            i = row4col[j];

        sc[j] = true;
        remaining[index] = remaining[--num_remaining];
    }

    *pmin = min;
    return sink;
}


void solve_rectangular_linear_sum_assignment(int nr, int nc, const float *restrict cost, float offset,
        int *restrict matched, float *restrict score)
{
    if(offset == INFINITY)
    {
        *score = NAN;
        *matched = -1;
        return;
    }

    // handle trivial inputs
    if(nr == 0 || nc == 0)
        return;

    // initialize variables
    float *restrict u = palloc(nr * sizeof(float));
    float *restrict v = palloc(nc * sizeof(float));
    float *restrict shortest_paths = palloc(nc * sizeof(float));
    int *restrict path = palloc(nc * sizeof(int));
    int *restrict col4row = palloc(nr * sizeof(int));
    int *restrict row4col = palloc(nc * sizeof(int));
    bool *restrict sr = palloc(nr * sizeof(bool));
    bool *restrict sc = palloc(nc * sizeof(bool));
    int *restrict remaining = palloc(nc * sizeof(int));
    bool infeasible = false;

    memset(u, 0, nr * sizeof(float));
    memset(v, 0, nc * sizeof(float));
    memset(path, -1, nc * sizeof(int));
    memset(col4row, -1, nr * sizeof(int));
    memset(row4col, -1, nc * sizeof(int));

    // iteratively build the solution
    for(int row = 0; row < nr; row++)
    {
        float min;
        int sink = augmenting_path(nr, nc, cost, offset, u, v, path, row4col, shortest_paths, row, sr, sc, remaining, &min);

        if(sink < 0)
        {
            infeasible = true;
            break;
        }

        // update dual variables
        u[row] += min;

        for(int i = 0; i < nr; i++)
            if(sr[i] && i != row)
                u[i] += min - shortest_paths[col4row[i]];

        for(int j = 0; j < nc; j++)
            if(sc[j])
                v[j] -= min - shortest_paths[j];

        // augment previous solution
        while(true)
        {
            int i = path[sink];
            row4col[sink] = i;

            int t = col4row[i];
            col4row[i] = sink;
            sink = t;

            if(i == row)
                break;
        }
    }

    if(!infeasible)
    {
        int cnt = 0;
        float sum = 0;

        for(int i = 0; i < nr; i++)
        {
            if(cost[i * (size_t) nc + col4row[i]] > 0)
            {
                sum += cost[i * (size_t) nc + col4row[i]];
                cnt++;
            }
        }

        *score += sum;
        *matched += cnt;
    }
    else
    {
        *score = NAN;
        *matched = -1;
    }

    pfree(remaining);
    pfree(sc);
    pfree(sr);
    pfree(row4col);
    pfree(col4row);
    pfree(path);
    pfree(shortest_paths);
    pfree(v);
    pfree(u);
}
