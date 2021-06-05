rm -rf ./data
mkdir data
rm -rf ./out
mkdir out

python3 distill.py
./biggraph.sh
python3 distill-re.py