#bin/bash

RPK_PATH="../vbuild/go/linux/bin/rpk"
DIR_PATH="./test"
SCRIPTS_DIR_PATH="./scripts"
INPUT="many_to_one"
DATA_DIR="./cluster/redpanda-node-0/data/kafka"
OUTPUT="output"

SCRIPTS_COUNT=5

MESSAGES_COUNT=50000
MESSAGES_SIZE=260000

rm -rf $SCRIPTS_DIR_PATH
mkdir $SCRIPTS_DIR_PATH

rm -rf $DIR_PATH
mkdir $DIR_PATH

$RPK_PATH wasm generate $DIR_PATH

template=`cat ./template_code.js`

new_code=${template//input/${INPUT}}

echo "$new_code" > $DIR_PATH/src/main.js

cd $DIR_PATH
npm install
npm run build
cd ..

cp $DIR_PATH/dist/main.js $SCRIPTS_DIR_PATH/${INPUT}.js

$RPK_PATH topic create ${INPUT}

for i in {1..100}
do
    # Deploy
    $RPK_PATH wasm deploy --name "${INPUT}_${i}" $SCRIPTS_DIR_PATH/${INPUT}.js
done

./rdkafka_perfomance -P -t "${INPUT}" -s  ${MESSAGES_SIZE} -b 127.0.0.1:9092,127.0.0.1:9093,127.0.0.1:9094 -m "qwertyuiopasdfghjklzxcvbnm" -c ${MESSAGES_COUNT}
total_count=$(($MESSAGES_COUNT * $SCRIPTS_COUNT))
./rdkafka_perfomance -C -t "${INPUT}.\$${OUTPUT}\$"  -b 127.0.0.1:9092,127.0.0.1:9093,127.0.0.1:9094 -p 0_11 -c ${total_count}

rm -rf rpk_log.txt
$RPK_PATH topic consume ${INPUT}.\$${OUTPUT}\$ &> rpk_log.txt &
sleep 10
pids=`ps aux | grep rpk | awk '{print $2}'`
sudo kill -9 $pids
count=`grep "qwertyuiopasdfghjklzxcvbnm" rpk_log.txt -c`
if ! [ "$count" == "0" ]
then
    echo "FAILFAILFAILFAILFAILFAIL"
fi
