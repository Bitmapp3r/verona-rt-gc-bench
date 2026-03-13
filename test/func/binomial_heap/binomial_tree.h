// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include <debug/harness.h>
#include <verona.h>

using namespace snmalloc;
using namespace verona::rt;
using namespace verona::rt::api;

struct Node : public V<Node>
{
  int value;
  Node* children = nullptr;
  Node* sib = nullptr;

  void trace(ObjectStack& st) const
  {
    if (children != nullptr)
      st.push(children);

    if (sib != nullptr)
      st.push(sib);
  }
};