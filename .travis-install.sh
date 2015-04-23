wget https://github.com/gidden/ciclus/archive/travis.zip -O ciclus.zip
unzip -j ciclus.zip "*/cycamore/*" -d conda-recipe
sed -i  "s/- cyclus/- cyclus 0.0/g" conda-recipe/meta.yaml
conda build --no-test conda-recipe

status=$?
if [[ $status != 0 ]]; then
    exit $status

conda install --use-local cycamore=0.0

status=$?
if [[ $status != 0 ]]; then
    exit $status
