#include "GoTools/lrsplines2D/Mesh2D.h"
#include "GoTools/utils/checks.h"
#include "GoTools/utils/StreamUtils.h"
#include "GoTools/lrsplines2D/Mesh2DIterator.h"
#include "GoTools/lrsplines2D/IndexMesh2DIterator.h"

#include <vector>
#include <assert.h>
#include <algorithm>
#include <stdexcept>

#include <array>

using namespace std;
namespace Go
{

namespace { // anonymous namespace

// This function just checks the integrity of the vector of meshrectangles.
// - Verifies that the entries in the vector is properly sorted.
// - Verifies that there are no trivial entries (describing intervals of zero
//   length.
// - Verifies that multiplicities are >= 0.
bool mrvec_is_correct(const vector<GPos>& vec);


};


// =============================================================================
Mesh2D::Mesh2D(std::istream& is) {read(is); }
// =============================================================================

// =============================================================================
void Mesh2D::write(std::ostream& os) const
// =============================================================================
{
  object_to_stream(os, knotvals_x_);
  object_to_stream(os, knotvals_y_);
  object_to_stream(os, mrects_x_);
  object_to_stream(os, mrects_y_);
}

// =============================================================================
void Mesh2D::read(std::istream& is)
// =============================================================================
{
  Mesh2D tmp;
  object_from_stream(is, tmp.knotvals_x_);
  object_from_stream(is, tmp.knotvals_y_);
  object_from_stream(is, tmp.mrects_x_);
  object_from_stream(is, tmp.mrects_y_);
  tmp.consistency_check_();
  swap(tmp);
}

// =============================================================================
void Mesh2D::swap(Mesh2D& rhs)
// =============================================================================
{
  std::swap(knotvals_x_, rhs.knotvals_x_);
  std::swap(knotvals_y_, rhs.knotvals_y_);
  std::swap(mrects_x_,   rhs.mrects_x_);
  std::swap(mrects_y_,   rhs.mrects_y_);
}

// =============================================================================
void Mesh2D::consistency_check_() const
// =============================================================================
{
  assert(mrects_x_.size() > 1);
  assert(mrects_y_.size() > 1);

  assert(strictly_increasing(knotvals_x_));
  assert(strictly_increasing(knotvals_y_));

  for (auto mv = mrects_x_.begin(); mv != mrects_x_.end(); ++mv)
    assert(mrvec_is_correct(*mv));
  for (auto mv = mrects_y_.begin(); mv != mrects_y_.end(); ++mv) 
    assert(mrvec_is_correct(*mv));

  // for (const auto& mv : mrects_x_) assert(mrvec_is_correct(mv));
  // for (const auto& mv : mrects_y_) assert(mrvec_is_correct(mv));
}

// =============================================================================
Mesh2DIterator Mesh2D::begin() const
// =============================================================================
{ 
  return Mesh2DIterator(*this, 0, 0);
}

// =============================================================================
Mesh2DIterator Mesh2D::end() const
// =============================================================================
{ 
  return Mesh2DIterator(*this, numDistinctKnots(XFIXED), numDistinctKnots(YFIXED));
}

// =============================================================================
IndexMesh2DIterator Mesh2D::indexMeshBegin() const
// =============================================================================
{ 
  return IndexMesh2DIterator(*this);
}

// =============================================================================
IndexMesh2DIterator Mesh2D::indexMeshEnd() const
// =============================================================================
{ 
  return IndexMesh2DIterator(*this, -1, -1, -1, -1);
}

// =============================================================================
int Mesh2D::nu(Direction2D d, int ix, int start, int end) const
// =============================================================================
{
  if (end >= numDistinctKnots(flip(d))) return 0; // proposed meshrectangle surpasses grid
  const auto& mr = select_meshvec_(d, ix);
  if (!(end > start)) return 0; // we can now safely assume that end > start

  int result = mr[0].mult;
  for (auto i = mr.begin(); i != mr.end(); ++i) 
    if      (i->ix <= start) result = i->mult;
    else if (i->ix >= end)   break; // finished
    else if (i->mult == 0)   return 0; // gap encountered - nu is zero
    else                     result = std::min(result, i->mult);
  
  return result;
}

// =============================================================================
int Mesh2D::extent(Direction2D d, int ix, int start, int mult) const
// =============================================================================
{
  const auto& mr = select_meshvec_(d, ix);
  auto p1 = find_if(mr.begin(), mr.end(), [start](const GPos& g) {return g.ix >= start;});
  if (p1 == mr.end() || p1->ix > start) --p1; // this will never break as long as start >= 0 
  auto p2 = find_if(p1, mr.end(), [mult](const GPos& g) {return g.mult < mult;});
  if (p2 == p1) return 0;
  else if (p2 == mr.end()) return numDistinctKnots(flip(d)) - 1 - start;
  else return p2->ix - start; 
}

// =============================================================================
void Mesh2D::incrementMult(Direction2D d, int ix, int start, int end, int mult)
// =============================================================================
{
  // The current implementation is dead simple but likely inefficient.  A more
  // efficient implementation could be done at a later stage if this proves to 
  // be a relevant bottleneck (but that might be unlikely...).

  for (int i = start; i < end; ++i) {
    const int cur_mult = nu(d, ix, i, i+1);
    setMult(d, ix, i, i+1, cur_mult + mult); // set the new multiplicity to the old one plus 'mult'
  }
}

// =============================================================================
void Mesh2D::setMult(Direction2D d, int ix, int start, int end, int mult)
// =============================================================================
{
  assert((end > start) && (mult >= 0)); // this is the contract for calling the function
  auto& mr = select_meshvec_(d, ix);
  const int last_pos = numDistinctKnots(flip(d)) - 1;
  vector<GPos> result;
  result.reserve(mr.size() + 1);

  // keeping preceeding GPos (not affected by the change)
  auto p = find_if(mr.begin(), mr.end(), [start](GPos& g) {return g.ix >= start;});
  result.insert(result.end(), mr.begin(), p);
  
  // inserting the new GPos
  if (result.empty() || result.back().mult != mult) 
    result.push_back(GPos(start, mult));
  if (end < last_pos) {
    int tmp = nu(d, ix, end, end+1);
    if (tmp != mult) result.push_back(GPos(end, tmp));
  }

  // inserting succeeding GPoses (not affected by the change)
  p = find_if(p, mr.end(), [end](GPos& g) {return g.ix > end;});
  result.insert(result.end(), p, mr.end());

  mr.swap(result);

  // verify that end contract of this function is fulfilled
  assert(mrvec_is_correct(mr));
}

// =============================================================================
int Mesh2D::insertLine(Direction2D d, double kval, int mult)
// =============================================================================
{
  // Mesh2D does not impose any specific tolerance, but assumes that there is one
  // in place at a higher level.  It thus accepts new knot values that are 
  // arbitrarily close to existing ones, but protests if the new knot value is 
  // _exactly_ the same (bit-wise) as an existing one, since this breaks with the
  // principle that the knot-vectors should be strictly increasing.
  vector<double>& kvec = (d == XFIXED) ? knotvals_x_ : knotvals_y_;

  auto p = find_if(kvec.begin(), kvec.end(), [kval](double d) {return d >= kval;});
  if (*p == kval) { throw runtime_error("Knotvalue already in vector."); }

  const int ix = (p - kvec.begin());  // this is the index of the line to insert
  
  auto& target = (d == XFIXED) ? mrects_x_ : mrects_y_;
  auto& other  = (d == XFIXED) ? mrects_y_ : mrects_x_;

  // inserting new line
  target.insert(target.begin() + ix, vector<GPos> (1, GPos(0, mult)));
  kvec.insert(p, kval);
  
  // adjust indexes in the other 
  for (auto gvec_it = other.begin(); gvec_it != other.end(); ++gvec_it)
    for (auto g_it = gvec_it->begin(); g_it != gvec_it->end(); ++g_it)
      if (g_it->ix >= ix) ++(g_it->ix);
  
  return ix;
}

// =============================================================================
std::vector<std::pair<int, int> > Mesh2D::segments(Direction2D d, int ix, int threshold) const
// =============================================================================
{
  const auto& mvec = select_meshvec_(d, ix);
  vector<pair<int, int> > result;
  const int BLANK = -1; // use this flag to indicate uninitialized value
  int start = BLANK;
  for (auto i = mvec.begin(); i != mvec.end(); ++i)
    if (i->mult >= threshold && start == BLANK) start = i->ix;
    else if (i->mult < threshold && start != BLANK) {
      result.emplace_back(pair<int, int>(start, i->ix));
      start = BLANK;
    }

  if (start != BLANK) 
    result.emplace_back(pair<int, int>(start, numDistinctKnots(flip(d)) - 1));

  return result;
}

// =============================================================================
int Mesh2D::largestMultInLine(Direction2D d, int ix) const
// =============================================================================
{
  const auto& mvec = select_meshvec_(d, ix);
  assert( ! mvec.empty());
  return max_element(mvec.begin(), 
		     mvec.end(), 
		     [](const GPos& a, const GPos& b) {return a.mult < b.mult;} )->mult;
}

// =============================================================================
namespace { // anonymous namespace
// =============================================================================

// =============================================================================
bool mrvec_is_correct(const vector<GPos>& v)
// =============================================================================
{
  int prev_ix = -1; // this would always be smaller than first index, which is zero
  for (auto m = v.begin(); m != v.end(); ++m) {
    if      (m->ix <= prev_ix) return false; // should be _strictly_ increasing.
    else if (m->mult < 0)      return false; // multiplicities should never be negative
    prev_ix = m->ix;
  }
  return true;
}

  
}; // end anonymous namespace

}; // end namespace Go
