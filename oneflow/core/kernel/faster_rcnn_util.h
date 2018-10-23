#ifndef ONEFLOW_CORE_KERNEL_FASTER_RCNN_UTIL_H_
#define ONEFLOW_CORE_KERNEL_FASTER_RCNN_UTIL_H_

#include "oneflow/core/common/util.h"
#include "oneflow/core/register/blob.h"
#include "oneflow/core/operator/op_conf.pb.h"

namespace oneflow {

template<typename T, size_t N>
class Serial;

template<template<typename> class Wrapper, typename ElemType, size_t N>
class Serial<Wrapper<ElemType>, N> {
 public:
  using ElemArray = std::array<ElemType, N>;
  using WrapperType = Wrapper<ElemType>;

  OF_DISALLOW_COPY_AND_MOVE(Serial);
  Serial() = delete;
  ~Serial() = delete;

  static const WrapperType* Cast(const ElemType* ptr) {
    return reinterpret_cast<const WrapperType*>(ptr);
  }
  static WrapperType* MutCast(ElemType* ptr) { return reinterpret_cast<WrapperType*>(ptr); }

  const ElemArray& elem() const { return elem_; }
  ElemArray& mut_elem() { return elem_; }

 private:
  ElemArray elem_;
};

template<typename T>
class BBoxDelta;

template<typename T>
class BBox final : public Serial<BBox<T>, 4> {
 public:
  using BBoxArray = typename Serial<BBox<T>, 4>::ElemArray;

  inline T x1() const { return this->elem()[0]; }
  inline T y1() const { return this->elem()[1]; }
  inline T x2() const { return this->elem()[2]; }
  inline T y2() const { return this->elem()[3]; }
  inline T center_x() const { return (x1() + x2()) / 2.0; }
  inline T center_y() const { return (y1() + y2()) / 2.0; }

  inline void set_x1(T x1) { this->mut_elem()[0] = x1; }
  inline void set_y1(T y1) { this->mut_elem()[1] = y1; }
  inline void set_x2(T x2) { this->mut_elem()[2] = x2; }
  inline void set_y2(T y2) { this->mut_elem()[3] = y2; }

  const BBoxArray& bbox() const { return this->elem(); }
  BBoxArray& mut_bbox() { return this->mut_elem(); }

  inline T width() const { return x2() - x1() + OneVal<T>::value; }
  inline T height() const { return y2() - y1() + OneVal<T>::value; }
  inline T Area() const { return width() * height(); }

  template<typename U>
  inline float InterOverUnion(const BBox<U>* other) const {
    const float iw = std::min<float>(x2(), other->x2()) - std::max<float>(x1(), other->x1()) + 1.f;
    if (iw <= 0) { return 0.f; }
    const float ih = std::min<float>(y2(), other->y2()) - std::max<float>(y1(), other->y1()) + 1.f;
    if (ih <= 0) { return 0.f; }
    const float inter = iw * ih;
    return inter / (Area() + other->Area() - inter);
  }

  void Transform(const BBox<T>* bbox, const BBoxDelta<T>* delta,
                 const BBoxRegressionWeights& bbox_reg_ws) {
    const float w = bbox->x2() - bbox->x1() + 1.0f;
    const float h = bbox->y2() - bbox->y1() + 1.0f;
    const float ctr_x = bbox->x1() + 0.5f * w;
    const float ctr_y = bbox->y1() + 0.5f * h;

    const float dx = delta->dx() / bbox_reg_ws.weight_x();
    const float dy = delta->dy() / bbox_reg_ws.weight_y();
    const float dw = delta->dw() / bbox_reg_ws.weight_w();
    const float dh = delta->dh() / bbox_reg_ws.weight_h();

    const float pred_ctr_x = dx * w + ctr_x;
    const float pred_ctr_y = dy * h + ctr_y;
    const float pred_w = std::exp(dw) * w;
    const float pred_h = std::exp(dh) * h;

    set_x1(pred_ctr_x - 0.5f * pred_w);
    set_y1(pred_ctr_y - 0.5f * pred_h);
    set_x2(pred_ctr_x + 0.5f * pred_w - 1.f);
    set_y2(pred_ctr_y + 0.5f * pred_h - 1.f);
  }

  void Clip(const int64_t height, const int64_t width) {
    set_x1(std::max<T>(std::min<T>(x1(), width - 1), 0));
    set_y1(std::max<T>(std::min<T>(y1(), height - 1), 0));
    set_x2(std::max<T>(std::min<T>(x2(), width - 1), 0));
    set_y2(std::max<T>(std::min<T>(y2(), height - 1), 0));
  }
};

template<typename T>
class BBox2 final : public Serial<BBox2<T>, 5> {
 public:
  using BBoxArray = typename Serial<BBox2<T>, 5>::ElemArray;

  inline int32_t batch_idx() const { return static_cast<int32_t>(this->elem()[0]); }
  inline T x1() const { return this->elem()[1]; }
  inline T y1() const { return this->elem()[2]; }
  inline T x2() const { return this->elem()[3]; }
  inline T y2() const { return this->elem()[4]; }

  inline T width() const { return x2() - x1() + OneVal<T>::value; }
  inline T height() const { return y2() - y1() + OneVal<T>::value; }
  inline T Area() const { return width() * height(); }
};

template<typename T>
class BBoxDelta final : public Serial<BBoxDelta<T>, 4> {
 public:
  inline T dx() const { return this->elem()[0]; }
  inline T dy() const { return this->elem()[1]; }
  inline T dw() const { return this->elem()[2]; }
  inline T dh() const { return this->elem()[3]; }

  inline void set_dx(T dx) { this->mut_elem()[0] = dx; }
  inline void set_dy(T dy) { this->mut_elem()[1] = dy; }
  inline void set_dw(T dw) { this->mut_elem()[2] = dw; }
  inline void set_dh(T dh) { this->mut_elem()[3] = dh; }

  template<typename U, typename K>
  void TransformInverse(const BBox<U>* bbox, const BBox<K>* target_bbox,
                        const BBoxRegressionWeights& bbox_reg_ws) {
    float w = bbox->x2() - bbox->x1() + 1.f;
    float h = bbox->y2() - bbox->y1() + 1.f;
    float ctr_x = bbox->x1() + 0.5f * w;
    float ctr_y = bbox->y1() + 0.5f * h;

    float t_w = target_bbox->x2() - target_bbox->x1() + 1.f;
    float t_h = target_bbox->y2() - target_bbox->y1() + 1.f;
    float t_ctr_x = target_bbox->x1() + 0.5f * t_w;
    float t_ctr_y = target_bbox->y1() + 0.5f * t_h;

    set_dx(bbox_reg_ws.weight_x() * (t_ctr_x - ctr_x) / w);
    set_dy(bbox_reg_ws.weight_y() * (t_ctr_y - ctr_y) / h);
    set_dw(bbox_reg_ws.weight_w() * std::log(t_w / w));
    set_dh(bbox_reg_ws.weight_h() * std::log(t_h / h));
  }
};

template<typename T>
class BBoxWeights final : public Serial<BBoxWeights<T>, 4> {
 public:
  inline T weight_x() const { return this->elem()[0]; }
  inline T weight_y() const { return this->elem()[1]; }
  inline T weight_w() const { return this->elem()[2]; }
  inline T weight_h() const { return this->elem()[3]; }

  inline void set_weight_x(T weight_x) { this->mut_elem()[0] = weight_x; }
  inline void set_weight_y(T weight_y) { this->mut_elem()[1] = weight_y; }
  inline void set_weight_w(T weight_w) { this->mut_elem()[2] = weight_w; }
  inline void set_weight_h(T weight_h) { this->mut_elem()[3] = weight_h; }
};

class Indexes {
 public:
  Indexes(size_t capacity, size_t size, int32_t* index_ptr, bool init_index)
      : capacity_(capacity), size_(size), index_ptr_(index_ptr) {
    if (init_index) { std::iota(index_ptr_, index_ptr_ + size_, 0); }
  }
  Indexes(size_t capacity, int32_t* index_ptr, bool init_index = false)
      : Indexes(capacity, capacity, index_ptr, init_index) {}

  void Truncate(size_t size) {
    CHECK_LE(size, capacity_);
    size_ = size;
  }

  void Assign(const Indexes& other) {
    CHECK_LE(other.size(), capacity_);
    FOR_RANGE(size_t, i, 0, other.size()) { index_ptr_[i] = other.GetIndex(i); }
    size_ = other.size();
  }

  Indexes Slice(size_t begin, size_t end) const {
    CHECK_LE(begin, end);
    CHECK_LE(end, size_);
    return Indexes(end - begin, index_ptr_ + begin, false);
  }

  void Concat(const Indexes& other) {
    CHECK_LE(other.size(), capacity_ - size_);
    FOR_RANGE(size_t, i, 0, other.size()) { index_ptr_[size_ + i] = other.GetIndex(i); }
    size_ += other.size();
  }

  void PushBack(int32_t index) {
    CHECK_LT(size_, capacity_);
    index_ptr_[size_++] = index;
  }

  size_t Find(const std::function<bool(int32_t)>& Condition) const {
    FOR_RANGE(size_t, i, 0, this->size()) {
      if (Condition(GetIndex(i))) { return i; }
    }
    return this->size();
  }

  void Filter(const std::function<bool(size_t, int32_t)>& FilterFunc) {
    size_t keep_num = 0;
    FOR_RANGE(size_t, i, 0, size_) {
      int32_t cur_index = GetIndex(i);
      if (!FilterFunc(i, cur_index)) {
        // keep_num <= i so index_ptr_ never be written before read
        mut_index_ptr()[keep_num++] = cur_index;
      }
    }
    size_ = keep_num;
  }

  void Sort(const std::function<bool(int32_t, int32_t)>& Compare) {
    std::sort(index_ptr_, index_ptr_ + size_,
              [&](int32_t lhs_index, int32_t rhs_index) { return Compare(lhs_index, rhs_index); });
  }

  void ArgSort(Indexes& buf) {
    // Warning: buf must be prefilled
    CHECK_EQ(buf.size(), size_);
    std::sort(buf.mut_index_ptr(), buf.mut_index_ptr() + size_,
              [&](int32_t lhs_index, int32_t rhs_index) {
                return GetIndex(lhs_index) < GetIndex(rhs_index);
              });
    Assign(buf);
  }

  void NthElement(size_t n, const std::function<bool(int32_t, int32_t)>& Compare) {
    std::nth_element(
        index_ptr_, index_ptr_ + n, index_ptr_ + size_,
        [&](int32_t lhs_index, int32_t rhs_index) { return Compare(lhs_index, rhs_index); });
  }

  void Shuffle(size_t begin, size_t end) {
    CHECK_LE(begin, end);
    CHECK_LE(end, size_);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::shuffle(index_ptr_ + begin, index_ptr_ + end, gen);
  }

  void Shuffle() { Shuffle(0, size_); }

  void ForEach(const std::function<bool(size_t, int32_t)>& DoNext) {
    FOR_RANGE(size_t, i, 0, size_) {
      if (!DoNext(i, GetIndex(i))) { break; }
    }
  }

  int32_t GetIndex(size_t n) const {
    CHECK_LT(n, size_);
    return index_ptr_[n];
  }

  size_t capacity() const { return capacity_; }
  size_t size() const { return size_; };
  const int32_t* index_ptr() const { return index_ptr_; }
  int32_t* mut_index_ptr() { return index_ptr_; }

 private:
  const size_t capacity_;
  size_t size_;
  int32_t* index_ptr_;
};

template<typename IndexType, typename T>
class BBoxIndex : public IndexType {
 public:
  BBoxIndex(const IndexType& indexes, const T* bbox_ptr)
      : IndexType(indexes), bbox_ptr_(bbox_ptr) {}

  void FilterByBBox(const std::function<bool(size_t, int32_t, const BBox<T>*)>& FilterFunc) {
    this->Filter([&](size_t n, int32_t index) { return FilterFunc(n, index, bbox(index)); });
  }

  void SortByBBox(const std::function<bool(const BBox<T>&, const BBox<T>&)>& Compare) {
    this->Sort([&](int32_t lhs_index, int32_t rhs_index) {
      return Compare(bbox(lhs_index), bbox(rhs_index));
    });
  }

  const BBox<T>* GetBBox(size_t n) const {
    CHECK_LT(n, this->size());
    return BBox<T>::Cast(bbox_ptr_) + this->GetIndex(n);
  }
  const BBox<T>* bbox(int32_t index) const {
    CHECK_GE(index, 0);
    return BBox<T>::Cast(bbox_ptr_) + index;
  }
  BBox<T>* mut_bbox(int32_t index) {
    CHECK_GE(index, 0);
    return BBox<T>::MutCast(bbox_ptr_) + index;
  }
  const T* bbox_ptr() const { return bbox_ptr_; }
  T* mut_bbox_ptr() { return bbox_ptr_; }

 private:
  const T* bbox_ptr_;
};

template<typename IndexType>
class LabelIndex : public IndexType {
 public:
  LabelIndex(const IndexType& indexes, int32_t* label_ptr)
      : IndexType(indexes), label_ptr_(label_ptr) {}

  void FillLabel(int32_t begin, int32_t end, int32_t label) {
    CHECK_GE(begin, 0);
    CHECK_GE(end, begin);
    std::fill(label_ptr_ + begin, label_ptr_ + end, label);
  }

  void FillLabel(int32_t label) { FillLabel(0, this->capacity(), label); }

  void SortByLabel(const std::function<bool(int32_t, int32_t)>& Compare) {
    this->Sort([&](int32_t lhs_index, int32_t rhs_index) {
      return Compare(label(lhs_index), label(rhs_index));
    });
  }

  size_t FindByLabel(const std::function<bool(int32_t)>& Condition) const {
    return this->Find([&](int32_t index) { return Condition(label(index)); });
  }

  void ForEachLabel(const std::function<bool(size_t, int32_t, int32_t)>& DoNext) {
    this->ForEach([&](size_t n, int32_t index) { return DoNext(n, index, label(index)); });
  }

  int32_t GetLabel(size_t n) const {
    CHECK_LT(n, this->size());
    return label(this->GetIndex(n));
  }

  void SetLabel(size_t n, int32_t label) {
    CHECK_LT(n, this->size());
    set_label(this->GetIndex(n), label);
  }

  int32_t label(int32_t index) const {
    CHECK_GE(index, 0);
    return label_ptr_[index];
  }
  void set_label(int32_t index, int32_t label) {
    CHECK_GE(index, 0);
    label_ptr_[index] = label;
  }
  const int32_t* label_ptr() const { return label_ptr_; }
  int32_t* mut_label_ptr() { return label_ptr_; }

 private:
  int32_t* label_ptr_;
};

template<typename IndexType, typename T>
class ScoreIndex : public IndexType {
 public:
  ScoreIndex(const IndexType& indexes, const T* score_ptr)
      : IndexType(indexes), score_ptr_(score_ptr) {}

  void SortByScore(const std::function<bool(T, T)>& Compare) {
    this->Sort([&](int32_t lhs_index, int32_t rhs_index) {
      return Compare(score(lhs_index), score(rhs_index));
    });
  }

  void NthElementByScore(size_t n, const std::function<bool(T, T)>& Compare) {
    this->NthElement(n, [&](int32_t lhs_index, int32_t rhs_index) {
      return Compare(score(lhs_index), score(rhs_index));
    });
  }

  size_t FindByScore(const std::function<bool(T)>& Condition) {
    return this->Find([&](int32_t index) { return Condition(score(index)); });
  }

  void FilterByScore(const std::function<bool(size_t, int32_t, T)>& FilterFunc) {
    this->Filter([&](size_t n, int32_t index) { return FilterFunc(n, index, score(index)); });
  }

  T GetScore(size_t n) const {
    CHECK_LT(n, this->size());
    return score(this->GetIndex(n));
  }
  T score(int32_t index) const {
    CHECK_GE(index, 0);
    return score_ptr_[index];
  }
  const T* score_ptr() const { return score_ptr_; }

 private:
  const T* score_ptr_;
};

template<typename IndexType>
class MaxOverlapIndex : public IndexType {
 public:
  MaxOverlapIndex(const IndexType& slice, float* max_overlap_ptr, int32_t* max_overlap_gt_index_ptr,
                  bool init_max_overlap)
      : IndexType(slice),
        max_overlap_ptr_(max_overlap_ptr),
        max_overlap_gt_index_ptr_(max_overlap_gt_index_ptr) {
    if (init_max_overlap) {
      memset(max_overlap_ptr_, 0, this->capacity() * sizeof(float));
      std::fill(max_overlap_gt_index_ptr_, max_overlap_gt_index_ptr_ + this->capacity(), -1);
    }
  }

  void UpdateMaxOverlap(int32_t index, int32_t gt_index, float overlap,
                        const std::function<void()>& DoUpdateHandle = []() {}) {
    CHECK_GE(index, 0);
    if (overlap > max_overlap(index)) {
      set_max_overlap(index, overlap);
      set_max_overlap_gt_index(index, gt_index);
      DoUpdateHandle();
    }
  }

  void SortByMaxOverlap(const std::function<bool(float, float)>& Compare) {
    this->Sort([&](int32_t lhs_index, int32_t rhs_index) {
      return Compare(max_overlap(lhs_index), max_overlap(rhs_index));
    });
  }

  size_t FindByMaxOverlap(const std::function<bool(float)>& Condition) {
    return this->Find([&](int32_t index) { return Condition(max_overlap(index)); });
  }

  void ForEachMaxOverlap(const std::function<bool(size_t, int32_t, float)>& DoNext) {
    this->ForEach([&](size_t n, int32_t index) { return DoNext(n, index, max_overlap(index)); });
  }

  float GetMaxOverlap(size_t n) const {
    CHECK_LT(n, this->size());
    return max_overlap(this->GetIndex(n));
  }
  int32_t GetMaxOverlapGtIndex(size_t n) const {
    CHECK_LT(n, this->size());
    return max_overlap_gt_index(this->GetIndex(n));
  }
  float max_overlap(int32_t index) const {
    if (index < 0) { return 1; }
    return max_overlap_ptr_[index];
  }
  void set_max_overlap(int32_t index, float overlap) {
    CHECK_GE(index, 0);
    max_overlap_ptr_[index] = overlap;
  }
  int32_t max_overlap_gt_index(int32_t index) const {
    if (index < 0) { return -index - 1; }
    return max_overlap_gt_index_ptr_[index];
  }
  void set_max_overlap_gt_index(int32_t index, int32_t gt_index) {
    CHECK_GE(index, 0);
    max_overlap_gt_index_ptr_[index] = gt_index;
  }

 private:
  float* max_overlap_ptr_;
  int32_t* max_overlap_gt_index_ptr_;
};

template<typename T>
using BoxesIndex = BBoxIndex<Indexes, T>;

template<typename T>
BoxesIndex<T> GenBoxesIndex(size_t capacity, int32_t* index_ptr, const T* boxes_ptr,
                            bool init_index = false) {
  return BoxesIndex<T>(Indexes(capacity, index_ptr, init_index), boxes_ptr);
}

template<typename T>
using ScoresIndex = ScoreIndex<Indexes, T>;

template<typename T>
ScoresIndex<T> GenScoresIndex(size_t capacity, int32_t* index_ptr, const T* score_ptr,
                              bool init_index = false) {
  return ScoresIndex<T>(Indexes(capacity, index_ptr, init_index), score_ptr);
}

template<typename T>
using ScoredBoxesIndex = ScoreIndex<BoxesIndex<T>, T>;

template<typename T>
ScoredBoxesIndex<T> GenScoredBoxesIndex(size_t capacity, int32_t* index_ptr, const T* boxes_ptr,
                                        const T* score_ptr, bool init_index = false) {
  return ScoredBoxesIndex<T>(BoxesIndex<T>(Indexes(capacity, index_ptr, init_index), boxes_ptr),
                             score_ptr);
}

}  // namespace oneflow

#endif  // ONEFLOW_CORE_KERNEL_FASTER_RCNN_UTIL_H_
