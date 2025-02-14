// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/invalidation/public/topic_invalidation_map.h"

#include <stddef.h>

#include "base/json/json_string_value_serializer.h"

namespace syncer {

TopicInvalidationMap::TopicInvalidationMap() {}

TopicInvalidationMap::TopicInvalidationMap(const TopicInvalidationMap& other) =
    default;

TopicInvalidationMap::~TopicInvalidationMap() {}

TopicSet TopicInvalidationMap::GetTopics() const {
  TopicSet ret;
  for (const auto& topic_and_invalidation_set : map_)
    ret.insert(topic_and_invalidation_set.first);
  return ret;
}

bool TopicInvalidationMap::Empty() const {
  return map_.empty();
}

void TopicInvalidationMap::Insert(const Invalidation& invalidation) {
  map_[invalidation.object_id().name()].Insert(invalidation);
}

TopicInvalidationMap TopicInvalidationMap::GetSubsetWithTopics(
    const TopicSet& topics) const {
  TopicToListMap new_map;
  for (const auto& topic : topics) {
    TopicToListMap::const_iterator lookup = map_.find(topic);
    if (lookup != map_.end()) {
      new_map[topic] = lookup->second;
    }
  }
  return TopicInvalidationMap(new_map);
}

const SingleObjectInvalidationSet& TopicInvalidationMap::ForTopic(
    Topic topic) const {
  TopicToListMap::const_iterator lookup = map_.find(topic);
  DCHECK(lookup != map_.end());
  DCHECK(!lookup->second.IsEmpty());
  return lookup->second;
}

void TopicInvalidationMap::GetAllInvalidations(
    std::vector<syncer::Invalidation>* out) const {
  for (TopicToListMap::const_iterator it = map_.begin(); it != map_.end();
       ++it) {
    out->insert(out->begin(), it->second.begin(), it->second.end());
  }
}

void TopicInvalidationMap::AcknowledgeAll() const {
  for (TopicToListMap::const_iterator it1 = map_.begin(); it1 != map_.end();
       ++it1) {
    for (SingleObjectInvalidationSet::const_iterator it2 = it1->second.begin();
         it2 != it1->second.end(); ++it2) {
      it2->Acknowledge();
    }
  }
}

bool TopicInvalidationMap::operator==(const TopicInvalidationMap& other) const {
  return map_ == other.map_;
}

std::unique_ptr<base::ListValue> TopicInvalidationMap::ToValue() const {
  std::unique_ptr<base::ListValue> value(new base::ListValue());
  for (TopicToListMap::const_iterator it1 = map_.begin(); it1 != map_.end();
       ++it1) {
    for (SingleObjectInvalidationSet::const_iterator it2 = it1->second.begin();
         it2 != it1->second.end(); ++it2) {
      value->Append(it2->ToValue());
    }
  }
  return value;
}

bool TopicInvalidationMap::ResetFromValue(const base::ListValue& value) {
  map_.clear();
  for (size_t i = 0; i < value.GetSize(); ++i) {
    const base::DictionaryValue* dict;
    if (!value.GetDictionary(i, &dict)) {
      return false;
    }
    std::unique_ptr<Invalidation> invalidation =
        Invalidation::InitFromValue(*dict);
    if (!invalidation) {
      return false;
    }
    Insert(*invalidation);
  }
  return true;
}

std::string TopicInvalidationMap::ToString() const {
  std::string output;
  JSONStringValueSerializer serializer(&output);
  serializer.set_pretty_print(true);
  serializer.Serialize(*ToValue());
  return output;
}

TopicInvalidationMap::TopicInvalidationMap(const TopicToListMap& map)
    : map_(map) {}

}  // namespace syncer
