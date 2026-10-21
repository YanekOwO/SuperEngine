// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2006, 2007 Elliot Glaysher
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
//
// -----------------------------------------------------------------------

#include "machine/rloperation/basic_types.hpp"

#include "libreallive/parser.hpp"
#include "machine/rloperation/reference_types.hpp"

// Implementation for IntConstant_T
IntConstant_T::type IntConstant_T::getData(
    RLMachine& machine,
    const libreallive::ExpressionPiecesVector& p,
    unsigned int& position) {
  return p[position++]->GetIntegerValue(machine);
}

IntReference_T::type IntReference_T::getData(
    RLMachine& machine,
    const libreallive::ExpressionPiecesVector& p,
    unsigned int& position) {
  return p[position++]->GetIntegerReferenceIterator(machine);
}

StrConstant_T::type StrConstant_T::getData(
    RLMachine& machine,
    const libreallive::ExpressionPiecesVector& p,
    unsigned int& position) {
  // When I was trying to get P_BRIDE running in rlvm, I noticed that when
  // loading a game, I would often crash with invalid iterators in the LRUCache
  // library, in ImageResources where we cached recently used images. After
  // adding some verification methods to it, it was obvious that it's internal
  // state was entirely screwed up. There were duplicates in the std::list<>
  // (it's a precondition that its elements are unique), there were entries in
  // the std::list<> that didn't exist in the corresponding std::map<>; the
  // data structure was a complete mess. I verified the datastructure before
  // and after each operation...and found that it would almost always be
  // corrupted in the check that happened at the beginning of each
  // function. Printing out the c_str() pointers showed there was no change
  // between the consistent data structure and the corrupted one; the
  // underlying pointers were the same.
  //
  // Setting a few watch points, the internal c_str() buffers were being
  // overwritten by a memcpy in libstdc++ which was called by boost::serialize
  // during the loading of the RLMachine's local memory.  But that makes
  // sense. Think about what happens when we execute the following kepago:
  //
  //   strS[0] = 'SOMEFILE'
  //   grpOpenBg(0, strS[0])
  //
  // We are assigning a value into the RLMachine's string memory. We are then
  // passing a copy-on-write string, backed by one memory buffer that contains
  // 'SOMEFILE' around. LRUCache and string memory now point to the same char*
  // buffer.
  //
  // boost::serialization appears to ignore the COW semantics of std::string
  // and just writes into it during a serialization::load() no matter how many
  // people hold references to the inner buffer. Now we have one screwed up
  // LRUCache data structure, and probably screwed up other places.
  //
  // So to fix this, we break the COW semantics here by forcing a copy. I'd
  // prefer to do this in RLMachine or Memory, but I can't because they return
  // references.
  string tmp = p[position++]->GetStringValue(machine);
  return string(tmp.data(), tmp.size());
}

StrReference_T::type StrReference_T::getData(
    RLMachine& machine,
    const libreallive::ExpressionPiecesVector& p,
    unsigned int& position) {
  return p[position++]->GetStringReferenceIterator(machine);
}
