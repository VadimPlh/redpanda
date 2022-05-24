# Copyright 2022 Redpanda Data, Inc.
#
# Use of this software is governed by the Business Source License
# included in the file licenses/BSL.md
#
# As of the Change Date specified in that file, in accordance with
# the Business Source License, use of this software will be governed
# by the Apache License, Version 2.0

from rptest.services.cluster import cluster
from rptest.clients.types import TopicSpec
from ducktape.utils.util import wait_until
from ducktape.mark import ignore
from rptest.services.admin import Admin
import confluent_kafka as ck
from rptest.tests.redpanda_test import RedpandaTest
from rptest.clients.rpk import RpkTool

class ConsumerGroupWithTxTest(RedpandaTest):
    topics = (TopicSpec(partition_count=3, replication_factor=3),
              TopicSpec(partition_count=3, replication_factor=3))

    def __init__(self, test_context):
        super(ConsumerGroupWithTxTest,
              self).__init__(test_context=test_context,
                             num_brokers=3,
                             extra_rp_conf={
                                 "enable_idempotence": True,
                                 "enable_transactions": True,
                                 "tx_timeout_delay_ms": 10000000,
                                 "abort_timed_out_transactions_interval_ms":
                                 10000000,
                                 'enable_leader_balancer': False
                             })

        self.admin = Admin(self.redpanda)
    
    @cluster(num_nodes=3)
    def get_consumer_group_metadata_test(self):
        producer = ck.Producer({
            'bootstrap.servers': self.redpanda.brokers(),
        })

        topic = self.topics[0]
        for i in range(100):
            for partition in range(topic.partition_count):
                producer.produce(topic.name, str(i), str(i), partition)
        producer.flush()

        conf = {
            'bootstrap.servers': self.redpanda.brokers(),
            'group.id': 'group1',
            'session.timeout.ms': 6000,
            'auto.offset.reset': 'earliest',
            'enable.auto.commit': False}
        consumer = ck.Consumer(conf)
        consumer.subscribe([topic.name])

        transaction = ck.Producer({
            'bootstrap.servers': self.redpanda.brokers(),
            'transactional.id': '1',
        })
        transaction.init_transactions()
        topic = self.topics[1]

        while True:
            ms = consumer.consume(num_messages = 10, timeout=10)
            if len(ms) == 0:
                break

            transaction.begin_transaction()
            for m in ms:
                for partition in range(topic.partition_count):
                    transaction.produce(topic.name, m.key(), m.value(), partition)

            transaction.send_offsets_to_transaction(consumer.position(consumer.assignment()), consumer.consumer_group_metadata())
            transaction.commit_transaction()
        
        transaction.flush()
        consumer.close()
