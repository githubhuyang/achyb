

torchbiggraph_import_from_tsv \
  --lhs-col=0 --rel-col=1 --rhs-col=2 \
  bgconfig.py \
  data/raw_graph.txt

torchbiggraph_train \
  bgconfig.py \
  -p edge_paths=data/partitioned

torchbiggraph_export_to_tsv \
  bgconfig.py \
  --entities-output data/embd.txt \
  --relation-types-output data/rel_types_paras.txt
