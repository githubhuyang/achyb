rm -r ./deserialized
mkdir deserialized
./bin/moonshine -dir $PWD/out
mv corpus.db distill.db