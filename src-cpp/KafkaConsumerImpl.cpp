/*
 * librdkafka - Apache Kafka C/C++ library
 *
 * Copyright (c) 2015 Magnus Edenhill
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <string>
#include <vector>

#include "rdkafkacpp_int.h"

RdKafka::KafkaConsumer::~KafkaConsumer () {}

RdKafka::KafkaConsumer *RdKafka::KafkaConsumer::create (RdKafka::Conf *conf,
                                                        std::string &errstr) {
  char errbuf[512];
  RdKafka::ConfImpl *confimpl = dynamic_cast<RdKafka::ConfImpl *>(conf);
  RdKafka::KafkaConsumerImpl *rkc = new RdKafka::KafkaConsumerImpl();
  rd_kafka_conf_t *rk_conf = NULL;
  size_t grlen;

  if (!confimpl->rk_conf_) {
    errstr = "Requires RdKafka::Conf::CONF_GLOBAL object";
    delete rkc;
    return NULL;
  }

  if (rd_kafka_conf_get(confimpl->rk_conf_, "group.id",
                        NULL, &grlen) != RD_KAFKA_CONF_OK ||
      grlen <= 1 /* terminating null only */) {
    errstr = "\"group.id\" must be configured";
    delete rkc;
    return NULL;
  }

  rkc->set_common_config(confimpl);

  rk_conf = rd_kafka_conf_dup(confimpl->rk_conf_);

  rd_kafka_t *rk;
  if (!(rk = rd_kafka_new(RD_KAFKA_CONSUMER, rk_conf,
                          errbuf, sizeof(errbuf)))) {
    errstr = errbuf;
    delete rkc;
    return NULL;
  }

  rkc->rk_ = rk;

  /* Redirect handle queue to cgrp's queue to provide a single queue point */
  rd_kafka_poll_set_consumer(rk);

  return rkc;
}







RdKafka::ErrorCode
RdKafka::KafkaConsumerImpl::subscribe (const std::vector<std::string> &topics) {
  rd_kafka_topic_partition_list_t *c_topics;
  rd_kafka_resp_err_t err;

  c_topics = rd_kafka_topic_partition_list_new(topics.size());

  for (unsigned int i = 0 ; i < topics.size() ; i++)
    rd_kafka_topic_partition_list_add(c_topics, topics[i].c_str(),
                                      RD_KAFKA_PARTITION_UA);

  err = rd_kafka_subscribe(rk_, c_topics);

  rd_kafka_topic_partition_list_destroy(c_topics);

  return static_cast<RdKafka::ErrorCode>(err);
}



RdKafka::ErrorCode
RdKafka::KafkaConsumerImpl::unsubscribe () {
  return static_cast<RdKafka::ErrorCode>(rd_kafka_unsubscribe(this->rk_));
}

RdKafka::Message *RdKafka::KafkaConsumerImpl::consume (int timeout_ms) {
  rd_kafka_message_t *rkmessage;

  rkmessage = rd_kafka_consumer_poll(this->rk_, timeout_ms);

  if (!rkmessage)
    return new RdKafka::MessageImpl(NULL, RdKafka::ERR__TIMED_OUT);

  return new RdKafka::MessageImpl(rkmessage);

}



RdKafka::ErrorCode
RdKafka::KafkaConsumerImpl::assignment (std::vector<RdKafka::TopicPartition*> &partitions) {
  rd_kafka_topic_partition_list_t *c_parts;
  rd_kafka_resp_err_t err;

  if ((err = rd_kafka_assignment(rk_, &c_parts)))
    return static_cast<RdKafka::ErrorCode>(err);

  partitions.resize(c_parts->cnt);

  for (int i = 0 ; i < c_parts->cnt ; i++)
    partitions[i] = new RdKafka::TopicPartitionImpl(&c_parts->elems[i]);

  rd_kafka_topic_partition_list_destroy(c_parts);

  return RdKafka::ERR_NO_ERROR;
}


RdKafka::ErrorCode
RdKafka::KafkaConsumerImpl::subscription (std::vector<std::string> &topics) {
  rd_kafka_topic_partition_list_t *c_topics;
  rd_kafka_resp_err_t err;

  if ((err = rd_kafka_subscription(rk_, &c_topics)))
    return static_cast<RdKafka::ErrorCode>(err);

  topics.resize(c_topics->cnt);
  for (int i = 0 ; i < c_topics->cnt ; i++)
    topics[i] = std::string(c_topics->elems[i].topic);

  rd_kafka_topic_partition_list_destroy(c_topics);

  return RdKafka::ERR_NO_ERROR;
}


RdKafka::ErrorCode
RdKafka::KafkaConsumerImpl::assign (const std::vector<TopicPartition*> &partitions) {
  rd_kafka_topic_partition_list_t *c_parts;
  rd_kafka_resp_err_t err;

  c_parts = partitions_to_c_parts(partitions);

  err = rd_kafka_assign(rk_, c_parts);

  rd_kafka_topic_partition_list_destroy(c_parts);
  return static_cast<RdKafka::ErrorCode>(err);
}


RdKafka::ErrorCode
RdKafka::KafkaConsumerImpl::unassign () {
  return static_cast<RdKafka::ErrorCode>(rd_kafka_assign(rk_, NULL));
}


RdKafka::ErrorCode
RdKafka::KafkaConsumerImpl::committed (std::vector<RdKafka::TopicPartition*> &partitions, int timeout_ms) {
  rd_kafka_topic_partition_list_t *c_parts;
  rd_kafka_resp_err_t err;

  c_parts = partitions_to_c_parts(partitions);

  err = rd_kafka_committed(rk_, c_parts, timeout_ms);

  if (!err) {
    update_partitions_from_c_parts(partitions, c_parts);
  }

  rd_kafka_topic_partition_list_destroy(c_parts);

  return static_cast<RdKafka::ErrorCode>(err);
}


RdKafka::ErrorCode
RdKafka::KafkaConsumerImpl::position (std::vector<RdKafka::TopicPartition*> &partitions) {
  rd_kafka_topic_partition_list_t *c_parts;
  rd_kafka_resp_err_t err;

  c_parts = partitions_to_c_parts(partitions);

  err = rd_kafka_position(rk_, c_parts);

  if (!err) {
    update_partitions_from_c_parts(partitions, c_parts);
  }

  rd_kafka_topic_partition_list_destroy(c_parts);

  return static_cast<RdKafka::ErrorCode>(err);
}




RdKafka::ErrorCode
RdKafka::KafkaConsumerImpl::close () {
  rd_kafka_resp_err_t err;
  err = rd_kafka_consumer_close(rk_);
  if (err)
    return static_cast<RdKafka::ErrorCode>(err);

  while (rd_kafka_outq_len(rk_) > 0)
    rd_kafka_poll(rk_, 10);
  rd_kafka_destroy(rk_);

  return static_cast<RdKafka::ErrorCode>(err);
}



