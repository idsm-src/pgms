select found_molecular_formula, found_inchi, found_pepmass, found_smiles, found_name, searched_smiles, searched_name, searched_molecular_formula, cosine_greedy_score from
(
  select 
    s.molecular_formula as found_molecular_formula, 
    s.inchi as found_inchi,
    s.pepmass as found_pepmass, 
    s.smiles as found_smiles, 
    s.name as found_name,
    q.smiles as searched_smiles, 
    q.name as searched_name,
    q.molecular_formula as searched_molecular_formula,
    pgms.cosine_greedy(s.spectrum, q.spectrum) as cosine_greedy_score 
      from spectrums as s,
  (
    select spectrum, smiles, name, molecular_formula from query
  ) as q
) as t
where cosine_greedy_score > 0.8
order by cosine_greedy_score desc;
