/*
   +----------------------------------------------------------------------+
   | HipHop for PHP                                                       |
   +----------------------------------------------------------------------+
   | Copyright (c) 2010-present Facebook, Inc. (http://www.facebook.com)  |
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.01 of the PHP license,      |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.php.net/license/3_01.txt                                  |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
*/
#include "hphp/runtime/vm/globals-array.h"

#include "hphp/runtime/base/array-init.h"
#include "hphp/runtime/base/array-iterator.h"
#include "hphp/runtime/base/member-val.h"
#include "hphp/runtime/base/mixed-array-defs.h"
#include "hphp/runtime/base/runtime-error.h"

namespace HPHP {

//////////////////////////////////////////////////////////////////////

static __thread GlobalsArray* g_variables;

GlobalsArray* get_global_variables() {
  assertx(g_variables != nullptr);
  return g_variables;
}

GlobalsArray::GlobalsArray(NameValueTable* tab)
  : ArrayData(kGlobalsKind)
  , m_tab(tab)
{
  Variant arr(staticEmptyArray());
#define X(s,v) tab->set(makeStaticString(#s), v.asTypedValue());

  X(argc,                 init_null_variant);
  X(argv,                 init_null_variant);
  X(_SERVER,              arr);
  X(_GET,                 arr);
  X(_POST,                arr);
  X(_COOKIE,              arr);
  X(_FILES,               arr);
  X(_ENV,                 arr);
  X(_REQUEST,             arr);
  X(_SESSION,             arr);
  X(HTTP_RAW_POST_DATA,   init_null_variant);
#undef X

  g_variables = this;
  assert(hasExactlyOneRef());
}

inline GlobalsArray* GlobalsArray::asGlobals(ArrayData* ad) {
  assert(ad->kind() == kGlobalsKind);
  return static_cast<GlobalsArray*>(ad);
}

inline const GlobalsArray*
GlobalsArray::asGlobals(const ArrayData* ad) {
  assert(ad->kind() == kGlobalsKind);
  return static_cast<const GlobalsArray*>(ad);
}

size_t GlobalsArray::Vsize(const ArrayData* ad) {
  // We need to iterate to find out the actual size, since kNamedLocalDataType
  // elements in the array may have been set to KindOfUninit.
  auto a = asGlobals(ad);
  if (a->m_tab->leaked()) return 0;
  size_t count = 0;
  auto iter_limit = IterEnd(a);
  for (auto iter = IterBegin(a);
       iter != iter_limit;
       iter = IterAdvance(a, iter)) {
    ++count;
  }
  return count;
}

Cell GlobalsArray::NvGetKey(const ArrayData* ad, ssize_t pos) {
  auto a = asGlobals(ad);
  NameValueTable::Iterator iter(a->m_tab, pos);
  if (iter.valid()) {
    auto k = iter.curKey();
    if (k->isRefCounted()) {
      k->incRefCount();
      return make_tv<KindOfString>(const_cast<StringData*>(k));
    }
    return make_tv<KindOfPersistentString>(k);
  }
  return make_tv<KindOfUninit>();
}

member_rval::ptr_u GlobalsArray::GetValueRef(const ArrayData* ad, ssize_t pos) {
  auto a = asGlobals(ad);
  NameValueTable::Iterator iter(a->m_tab, pos);
  return iter.valid() ? iter.curVal() : uninit_variant.asTypedValue();
}

bool
GlobalsArray::ExistsInt(const ArrayData* ad, int64_t k) {
  return ExistsStr(ad, String(k).get());
}

bool
GlobalsArray::ExistsStr(const ArrayData* ad, const StringData* k) {
  return asGlobals(ad)->m_tab->lookup(k) != nullptr;
}

member_rval::ptr_u
GlobalsArray::NvGetStr(const ArrayData* ad, const StringData* k) {
  return asGlobals(ad)->m_tab->lookup(k);
}

member_rval::ptr_u
GlobalsArray::NvGetInt(const ArrayData* ad, int64_t k) {
  return asGlobals(ad)->m_tab->lookup(String(k).get());
}

member_lval GlobalsArray::LvalInt(ArrayData* ad, int64_t k, bool copy) {
  return LvalStr(ad, String(k).get(), copy);
}

member_lval GlobalsArray::LvalStr(ArrayData* ad, StringData* k, bool /*copy*/) {
  auto a = asGlobals(ad);
  TypedValue* tv = a->m_tab->lookup(k);
  if (!tv) {
    TypedValue nulVal;
    tvWriteNull(&nulVal);
    tv = a->m_tab->set(k, &nulVal);
  }
  return member_lval { a, tv };
}

member_lval GlobalsArray::LvalNew(ArrayData* ad, bool /*copy*/) {
  return member_lval { ad, lvalBlackHole().asTypedValue() };
}

ArrayData* GlobalsArray::SetInt(ArrayData* ad,
                                         int64_t k,
                                         Cell v,
                                         bool copy) {
  return SetStr(ad, String(k).get(), v, copy);
}

ArrayData*
GlobalsArray::SetStr(ArrayData* ad, StringData* k, Cell v, bool /*copy*/) {
  auto a = asGlobals(ad);
  cellSet(v, *tvToCell(a->m_tab->lookupAdd(k)));
  return a;
}

ArrayData* GlobalsArray::SetRefInt(ArrayData* ad,
                                            int64_t k,
                                            Variant& v,
                                            bool copy) {
  return asGlobals(ad)->setRef(String(k).get(), v, copy);
}

ArrayData* GlobalsArray::SetRefStr(ArrayData* ad, StringData* k, Variant& v,
                                   bool /*copy*/) {
  auto a = asGlobals(ad);
  tvAsVariant(a->m_tab->lookupAdd(k)).assignRef(v);
  return a;
}

ArrayData*
GlobalsArray::RemoveInt(ArrayData* ad, int64_t k, bool copy) {
  return RemoveStr(ad, String(k).get(), copy);
}

ArrayData*
GlobalsArray::RemoveStr(ArrayData* ad, const StringData* k, bool /*copy*/) {
  auto a = asGlobals(ad);
  a->m_tab->unset(k);
  return a;
}

/*
 * The messages in the user-visible exceptions below claim we are
 * $GLOBALS, because the only user-visible GlobalsArray array
 * is currently $GLOBALS.
 */

ArrayData* GlobalsArray::Append(ArrayData*, Cell /*v*/, bool /*copy*/) {
  throw_not_implemented("append on $GLOBALS");
}

ArrayData* GlobalsArray::AppendRef(ArrayData*, Variant&, bool) {
  throw_not_implemented("appendRef on $GLOBALS");
}

ArrayData* GlobalsArray::AppendWithRef(ArrayData*, TypedValue, bool) {
  throw_not_implemented("appendWithRef on $GLOBALS");
}

ArrayData* GlobalsArray::PlusEq(ArrayData*, const ArrayData*) {
  throw_not_implemented("plus on $GLOBALS");
}

ArrayData* GlobalsArray::Merge(ArrayData*, const ArrayData*) {
  throw_not_implemented("merge on $GLOBALS");
}

ArrayData* GlobalsArray::Prepend(ArrayData*, Cell, bool) {
  throw_not_implemented("prepend on $GLOBALS");
}

ssize_t GlobalsArray::IterBegin(const ArrayData* ad) {
  auto a = asGlobals(ad);
  NameValueTable::Iterator iter(a->m_tab);
  return iter.toInteger();
}

ssize_t GlobalsArray::IterLast(const ArrayData* ad) {
  auto a = asGlobals(ad);
  return NameValueTable::Iterator::getLast(a->m_tab).toInteger();
}

ssize_t GlobalsArray::IterEnd(const ArrayData* ad) {
  auto a = asGlobals(ad);
  return NameValueTable::Iterator::getEnd(a->m_tab).toInteger();
}

ssize_t GlobalsArray::IterAdvance(const ArrayData* ad, ssize_t prev) {
  auto a = asGlobals(ad);
  NameValueTable::Iterator iter(a->m_tab, prev);
  iter.next();
  return iter.toInteger();
}

ssize_t GlobalsArray::IterRewind(const ArrayData* ad, ssize_t prev) {
  auto a = asGlobals(ad);
  NameValueTable::Iterator iter(a->m_tab, prev);
  iter.prev();
  return iter.toInteger();
}

bool
GlobalsArray::ValidMArrayIter(const ArrayData* ad,
                                       const MArrayIter & fp) {
  assert(fp.getContainer() == ad);
  auto a = asGlobals(ad);
  if (fp.getResetFlag()) return false;
  if (fp.m_pos == IterEnd(a)) return false;
  NameValueTable::Iterator iter(a->m_tab, fp.m_pos);
  return iter.valid();
}

bool GlobalsArray::AdvanceMArrayIter(ArrayData* ad, MArrayIter& fp) {
  auto a = asGlobals(ad);
  bool reset = fp.getResetFlag();
  NameValueTable::Iterator iter = reset ?
    NameValueTable::Iterator(a->m_tab) :
    NameValueTable::Iterator(a->m_tab, fp.m_pos);
  if (reset) {
    fp.setResetFlag(false);
  } else {
    if (!iter.valid()) {
      return false;
    }
    iter.next();
  }
  fp.m_pos = iter.toInteger();
  if (!iter.valid()) return false;
  // To conform to PHP behavior, we need to set the internal
  // cursor to point to the next element.
  iter.next();
  a->m_pos = iter.toInteger();
  return true;
}

ArrayData* GlobalsArray::EscalateForSort(ArrayData* ad, SortFunction /*sf*/) {
  raise_warning("Sorting the $GLOBALS array is not supported");
  return ad;
}
void GlobalsArray::Ksort(ArrayData*, int /*sort_flags*/, bool /*ascending*/) {}
void GlobalsArray::Sort(ArrayData*, int /*sort_flags*/, bool /*ascending*/) {}
void GlobalsArray::Asort(ArrayData*, int /*sort_flags*/, bool /*ascending*/) {}
bool GlobalsArray::Uksort(ArrayData*, const Variant& /*cmp_function*/) {
  return false;
}
bool GlobalsArray::Usort(ArrayData*, const Variant& /*cmp_function*/) {
  return false;
}
bool GlobalsArray::Uasort(ArrayData*, const Variant& /*cmp_function*/) {
  return false;
}

bool GlobalsArray::IsVectorData(const ArrayData*) {
  return false;
}

ArrayData* GlobalsArray::CopyStatic(const ArrayData*) {
  raise_fatal_error("GlobalsArray::copyStatic "
    "not implemented.");
}

void GlobalsArray::Renumber(ArrayData*) {}

void GlobalsArray::OnSetEvalScalar(ArrayData*) {
  not_reached();
}

//////////////////////////////////////////////////////////////////////

}
