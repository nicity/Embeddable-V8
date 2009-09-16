// Copyright 2009 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "v8.h"

#include "heap-profiler.h"
#include "string-stream.h"

namespace v8 {
namespace internal {


#ifdef ENABLE_LOGGING_AND_PROFILING
namespace {

// JSStatsHelper provides service functions for examining
// JS objects allocated on heap. It is run during garbage
// collection cycle, thus it doesn't need to use handles.
class JSStatsHelper {
 public:
  static int CalculateNetworkSize(JSObject* obj);
 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(JSStatsHelper);
};


int JSStatsHelper::CalculateNetworkSize(JSObject* obj) {
  int size = obj->Size();
  // If 'properties' and 'elements' are non-empty (thus, non-shared),
  // take their size into account.
  if (FixedArray::cast(obj->properties())->length() != 0) {
    size += obj->properties()->Size();
  }
  if (FixedArray::cast(obj->elements())->length() != 0) {
    size += obj->elements()->Size();
  }
  return size;
}


// A helper class for recording back references.
class ReferencesExtractor : public ObjectVisitor {
 public:
  ReferencesExtractor(
      const JSObjectsCluster& cluster, RetainerHeapProfile* profile)
      : cluster_(cluster),
        profile_(profile),
        inside_array_(false) {
  }

  void VisitPointer(Object** o) {
    if ((*o)->IsJSObject() || (*o)->IsString()) {
      profile_->StoreReference(cluster_, *o);
    } else if ((*o)->IsFixedArray() && !inside_array_) {
      // Traverse one level deep for data members that are fixed arrays.
      // This covers the case of 'elements' and 'properties' of JSObject,
      // and function contexts.
      inside_array_ = true;
      FixedArray::cast(*o)->Iterate(this);
      inside_array_ = false;
    }
  }

  void VisitPointers(Object** start, Object** end) {
    for (Object** p = start; p < end; p++) VisitPointer(p);
  }

 private:
  const JSObjectsCluster& cluster_;
  RetainerHeapProfile* profile_;
  bool inside_array_;
};


// A printer interface implementation for the Retainers profile.
class RetainersPrinter : public RetainerHeapProfile::Printer {
 public:
  void PrintRetainers(const StringStream& retainers) {
    LOG(HeapSampleJSRetainersEvent(*(retainers.ToCString())));
  }
};

}  // namespace


const ConstructorHeapProfile::TreeConfig::Key
    ConstructorHeapProfile::TreeConfig::kNoKey = NULL;
const ConstructorHeapProfile::TreeConfig::Value
    ConstructorHeapProfile::TreeConfig::kNoValue;


ConstructorHeapProfile::ConstructorHeapProfile()
    : zscope_(DELETE_ON_EXIT) {
}


void ConstructorHeapProfile::Call(String* name,
                                const NumberAndSizeInfo& number_and_size) {
  ASSERT(name != NULL);
  SmartPointer<char> s_name(
      name->ToCString(DISALLOW_NULLS, ROBUST_STRING_TRAVERSAL));
  LOG(HeapSampleJSConstructorEvent(*s_name,
                                   number_and_size.number(),
                                   number_and_size.bytes()));
}


void ConstructorHeapProfile::CollectStats(HeapObject* obj) {
  String* constructor = NULL;
  int size;
  if (obj->IsString()) {
    constructor = Heap::String_symbol();
    size = obj->Size();
  } else if (obj->IsJSObject()) {
    JSObject* js_obj = JSObject::cast(obj);
    constructor = js_obj->constructor_name();
    size = JSStatsHelper::CalculateNetworkSize(js_obj);
  } else {
    return;
  }

  JSObjectsInfoTree::Locator loc;
  if (!js_objects_info_tree_.Find(constructor, &loc)) {
    js_objects_info_tree_.Insert(constructor, &loc);
  }
  NumberAndSizeInfo number_and_size = loc.value();
  number_and_size.increment_number(1);
  number_and_size.increment_bytes(size);
  loc.set_value(number_and_size);
}


void ConstructorHeapProfile::PrintStats() {
  js_objects_info_tree_.ForEach(this);
}


void JSObjectsCluster::Print(StringStream* accumulator) const {
  ASSERT(!is_null());
  if (constructor_ == FromSpecialCase(ROOTS)) {
    accumulator->Add("(roots)");
  } else if (constructor_ == FromSpecialCase(GLOBAL_PROPERTY)) {
    accumulator->Add("(global property)");
  } else {
    SmartPointer<char> s_name(
        constructor_->ToCString(DISALLOW_NULLS, ROBUST_STRING_TRAVERSAL));
    accumulator->Add("%s", (*s_name)[0] != '\0' ? *s_name : "(anonymous)");
    if (instance_ != NULL) {
      accumulator->Add(":%p", static_cast<void*>(instance_));
    }
  }
}


void JSObjectsCluster::DebugPrint(StringStream* accumulator) const {
  if (!is_null()) {
    Print(accumulator);
  } else {
    accumulator->Add("(null cluster)");
  }
}


inline ClustersCoarser::ClusterBackRefs::ClusterBackRefs(
    const JSObjectsCluster& cluster_)
    : cluster(cluster_), refs(kInitialBackrefsListCapacity) {
}


inline ClustersCoarser::ClusterBackRefs::ClusterBackRefs(
    const ClustersCoarser::ClusterBackRefs& src)
    : cluster(src.cluster), refs(src.refs.capacity()) {
  refs.AddAll(src.refs);
}


inline ClustersCoarser::ClusterBackRefs&
    ClustersCoarser::ClusterBackRefs::operator=(
    const ClustersCoarser::ClusterBackRefs& src) {
  if (this == &src) return *this;
  cluster = src.cluster;
  refs.Clear();
  refs.AddAll(src.refs);
  return *this;
}


inline int ClustersCoarser::ClusterBackRefs::Compare(
    const ClustersCoarser::ClusterBackRefs& a,
    const ClustersCoarser::ClusterBackRefs& b) {
  int cmp = JSObjectsCluster::CompareConstructors(a.cluster, b.cluster);
  if (cmp != 0) return cmp;
  if (a.refs.length() < b.refs.length()) return -1;
  if (a.refs.length() > b.refs.length()) return 1;
  for (int i = 0; i < a.refs.length(); ++i) {
    int cmp = JSObjectsCluster::Compare(a.refs[i], b.refs[i]);
    if (cmp != 0) return cmp;
  }
  return 0;
}


ClustersCoarser::ClustersCoarser()
  : zscope_(DELETE_ON_EXIT),
    sim_list_(ClustersCoarser::kInitialSimilarityListCapacity),
    current_pair_(NULL) {
}


void ClustersCoarser::Call(
    const JSObjectsCluster& cluster, JSObjectsClusterTree* tree) {
  if (tree != NULL) {
    // First level of retainer graph.
    if (!cluster.can_be_coarsed()) return;
    ClusterBackRefs pair(cluster);
    ASSERT(current_pair_ == NULL);
    current_pair_ = &pair;
    current_set_ = new JSObjectsClusterTree();
    tree->ForEach(this);
    sim_list_.Add(pair);
    current_pair_ = NULL;
    current_set_ = NULL;
  } else {
    // Second level of retainer graph.
    ASSERT(current_pair_ != NULL);
    ASSERT(current_set_ != NULL);
    JSObjectsCluster eq = GetCoarseEquivalent(cluster);
    JSObjectsClusterTree::Locator loc;
    if (!eq.is_null()) {
      if (current_set_->Find(eq, &loc)) return;
      current_pair_->refs.Add(eq);
      current_set_->Insert(eq, &loc);
    } else {
      current_pair_->refs.Add(cluster);
    }
  }
}


void ClustersCoarser::Process(JSObjectsClusterTree* tree) {
  int last_eq_clusters = -1;
  for (int i = 0; i < kMaxPassesCount; ++i) {
    sim_list_.Clear();
    const int curr_eq_clusters = DoProcess(tree);
    // If no new cluster equivalents discovered, abort processing.
    if (last_eq_clusters == curr_eq_clusters) break;
    last_eq_clusters = curr_eq_clusters;
  }
}


int ClustersCoarser::DoProcess(JSObjectsClusterTree* tree) {
  tree->ForEach(this);
  // To sort similarity list properly, references list of a cluster is
  // required to be sorted, thus 'O1 <- A, B' and 'O2 <- B, A' would
  // be considered equivalent. But we don't sort them explicitly
  // because we know that they come from a splay tree traversal, so
  // they are already sorted.
  sim_list_.Sort(ClusterBackRefsCmp);
  return FillEqualityTree();
}


JSObjectsCluster ClustersCoarser::GetCoarseEquivalent(
    const JSObjectsCluster& cluster) {
  if (!cluster.can_be_coarsed()) return JSObjectsCluster();
  EqualityTree::Locator loc;
  return eq_tree_.Find(cluster, &loc) ? loc.value() : JSObjectsCluster();
}


bool ClustersCoarser::HasAnEquivalent(const JSObjectsCluster& cluster) {
  // Return true for coarsible clusters that have a non-identical equivalent.
  return cluster.can_be_coarsed() &&
      JSObjectsCluster::Compare(cluster, GetCoarseEquivalent(cluster)) != 0;
}


int ClustersCoarser::FillEqualityTree() {
  int eq_clusters_count = 0;
  int eq_to = 0;
  bool first_added = false;
  for (int i = 1; i < sim_list_.length(); ++i) {
    if (ClusterBackRefs::Compare(sim_list_[i], sim_list_[eq_to]) == 0) {
      EqualityTree::Locator loc;
      if (!first_added) {
        // Add self-equivalence, if we have more than one item in this
        // equivalence class.
        eq_tree_.Insert(sim_list_[eq_to].cluster, &loc);
        loc.set_value(sim_list_[eq_to].cluster);
        first_added = true;
      }
      eq_tree_.Insert(sim_list_[i].cluster, &loc);
      loc.set_value(sim_list_[eq_to].cluster);
      ++eq_clusters_count;
    } else {
      eq_to = i;
      first_added = false;
    }
  }
  return eq_clusters_count;
}


const JSObjectsCluster ClustersCoarser::ClusterEqualityConfig::kNoKey;
const JSObjectsCluster ClustersCoarser::ClusterEqualityConfig::kNoValue;
const JSObjectsClusterTreeConfig::Key JSObjectsClusterTreeConfig::kNoKey;
const JSObjectsClusterTreeConfig::Value JSObjectsClusterTreeConfig::kNoValue =
    NULL;


RetainerHeapProfile::RetainerHeapProfile()
    : zscope_(DELETE_ON_EXIT),
      coarse_cluster_tree_(NULL),
      retainers_printed_(0),
      current_printer_(NULL),
      current_stream_(NULL) {
  JSObjectsCluster roots(JSObjectsCluster::ROOTS);
  ReferencesExtractor extractor(
      roots, this);
  Heap::IterateRoots(&extractor);
}


JSObjectsCluster RetainerHeapProfile::Clusterize(Object* obj) {
  if (obj->IsJSObject()) {
    String* constructor = JSObject::cast(obj)->constructor_name();
    // Differentiate Object and Array instances.
    if (constructor == Heap::Object_symbol() ||
        constructor == Heap::Array_symbol()) {
      return JSObjectsCluster(constructor, obj);
    } else {
      return JSObjectsCluster(constructor);
    }
  } else if (obj->IsString()) {
    return JSObjectsCluster(Heap::String_symbol());
  } else {
    UNREACHABLE();
    return JSObjectsCluster();
  }
}


void RetainerHeapProfile::StoreReference(
    const JSObjectsCluster& cluster,
    Object* ref) {
  JSObjectsCluster ref_cluster = Clusterize(ref);
  JSObjectsClusterTree::Locator ref_loc;
  if (retainers_tree_.Insert(ref_cluster, &ref_loc)) {
    ref_loc.set_value(new JSObjectsClusterTree());
  }
  JSObjectsClusterTree* referenced_by = ref_loc.value();
  JSObjectsClusterTree::Locator obj_loc;
  referenced_by->Insert(cluster, &obj_loc);
}


void RetainerHeapProfile::CollectStats(HeapObject* obj) {
  if (obj->IsJSObject()) {
    const JSObjectsCluster cluster = Clusterize(JSObject::cast(obj));
    ReferencesExtractor extractor(cluster, this);
    obj->Iterate(&extractor);
  } else if (obj->IsJSGlobalPropertyCell()) {
    JSObjectsCluster global_prop(JSObjectsCluster::GLOBAL_PROPERTY);
    ReferencesExtractor extractor(global_prop, this);
    obj->Iterate(&extractor);
  }
}


void RetainerHeapProfile::DebugPrintStats(
    RetainerHeapProfile::Printer* printer) {
  coarser_.Process(&retainers_tree_);
  ASSERT(current_printer_ == NULL);
  current_printer_ = printer;
  retainers_tree_.ForEach(this);
  current_printer_ = NULL;
}


void RetainerHeapProfile::PrintStats() {
  RetainersPrinter printer;
  DebugPrintStats(&printer);
}


void RetainerHeapProfile::Call(
    const JSObjectsCluster& cluster,
    JSObjectsClusterTree* tree) {
  ASSERT(current_printer_ != NULL);
  if (tree != NULL) {
    // First level of retainer graph.
    if (coarser_.HasAnEquivalent(cluster)) return;
    ASSERT(current_stream_ == NULL);
    HeapStringAllocator allocator;
    StringStream stream(&allocator);
    current_stream_ = &stream;
    cluster.Print(current_stream_);
    ASSERT(coarse_cluster_tree_ == NULL);
    coarse_cluster_tree_ = new JSObjectsClusterTree();
    retainers_printed_ = 0;
    tree->ForEach(this);
    coarse_cluster_tree_ = NULL;
    current_printer_->PrintRetainers(stream);
    current_stream_ = NULL;
  } else {
    // Second level of retainer graph.
    ASSERT(coarse_cluster_tree_ != NULL);
    ASSERT(current_stream_ != NULL);
    if (retainers_printed_ >= kMaxRetainersToPrint) {
      if (retainers_printed_ == kMaxRetainersToPrint) {
        // TODO(mnaganov): Print the exact count.
        current_stream_->Add(",...");
        ++retainers_printed_;  // avoid printing ellipsis next time.
      }
      return;
    }
    JSObjectsCluster eq = coarser_.GetCoarseEquivalent(cluster);
    if (eq.is_null()) {
      current_stream_->Put(',');
      cluster.Print(current_stream_);
      ++retainers_printed_;
    } else {
      JSObjectsClusterTree::Locator loc;
      if (coarse_cluster_tree_->Insert(eq, &loc)) {
        current_stream_->Put(',');
        eq.Print(current_stream_);
        ++retainers_printed_;
      }
    }
  }
}


//
// HeapProfiler class implementation.
//
void HeapProfiler::CollectStats(HeapObject* obj, HistogramInfo* info) {
  InstanceType type = obj->map()->instance_type();
  ASSERT(0 <= type && type <= LAST_TYPE);
  info[type].increment_number(1);
  info[type].increment_bytes(obj->Size());
}


void HeapProfiler::WriteSample() {
  LOG(HeapSampleBeginEvent("Heap", "allocated"));
  LOG(HeapSampleStats(
      "Heap", "allocated", Heap::Capacity(), Heap::SizeOfObjects()));

  HistogramInfo info[LAST_TYPE+1];
#define DEF_TYPE_NAME(name) info[name].set_name(#name);
  INSTANCE_TYPE_LIST(DEF_TYPE_NAME)
#undef DEF_TYPE_NAME

  ConstructorHeapProfile js_cons_profile;
  RetainerHeapProfile js_retainer_profile;
  HeapIterator iterator;
  while (iterator.has_next()) {
    HeapObject* obj = iterator.next();
    CollectStats(obj, info);
    js_cons_profile.CollectStats(obj);
    js_retainer_profile.CollectStats(obj);
  }

  // Lump all the string types together.
  int string_number = 0;
  int string_bytes = 0;
#define INCREMENT_SIZE(type, size, name, camel_name)   \
    string_number += info[type].number();              \
    string_bytes += info[type].bytes();
  STRING_TYPE_LIST(INCREMENT_SIZE)
#undef INCREMENT_SIZE
  if (string_bytes > 0) {
    LOG(HeapSampleItemEvent("STRING_TYPE", string_number, string_bytes));
  }

  for (int i = FIRST_NONSTRING_TYPE; i <= LAST_TYPE; ++i) {
    if (info[i].bytes() > 0) {
      LOG(HeapSampleItemEvent(info[i].name(), info[i].number(),
                              info[i].bytes()));
    }
  }

  js_cons_profile.PrintStats();
  js_retainer_profile.PrintStats();

  LOG(HeapSampleEndEvent("Heap", "allocated"));
}


#endif  // ENABLE_LOGGING_AND_PROFILING


} }  // namespace v8::internal