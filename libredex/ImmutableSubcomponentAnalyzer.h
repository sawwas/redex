/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <functional>
#include <limits>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include <boost/optional.hpp>

#include "ControlFlow.h"
#include "DexClass.h"

namespace isa_impl {

// Forward declarations.
class AbstractAccessPath;
class Analyzer;

} // namespace isa_impl

enum class AccessPathKind { Parameter, Local, FinalField, Unknown };

/*
 * This analysis is aimed at identifying the components and subcomponents of
 * immutable data structures accessed via sequences of getters. For example,
 * consider the following Java method:
 *
 *   void doSomething(ImmutableStructure s) {
 *     A a = s.getA();
 *     B b = s.getB();
 *     ...
 *     C c = b.getC();
 *     doSomethingElse(c, a.getD().getE());
 *     ...
 *   }
 *
 * The analysis will automatically discover that in the call to
 * `doSomethingElse`, the first argument is the subcomponent `s.getB().get(C)`,
 * whereas the second argument refers to `s.getA().getD().getE()`. The analysis
 * assumes that the immutable structures are passed as arguments to the method
 * analyzed. The identification of calls to getter methods is done via a
 * user-provided predicate on method descriptors.
 *
 * There is no set notion of immutability in the Java language specification.
 * What we mean here is an object that behaves as if its subcomponents were
 * constant in a given context, even though the getters might alter its internal
 * structure under the hood. This analysis should only be used in well-defined
 * situations where it's been established that some parameters of certain
 * methods are immutable.
 */

/*
 * An access path is a sequence of getters originating from an unambiguous
 * register (for instance, a param register) of the method analyzed.
 *
 * Examples:
 *
 *   p0.getA().getB()
 *   p1.getC()
 *   p2               (an empty access path, i.e., the value of parameter #2)
 */
class AccessPath final {
 public:
  // The default constructor is required by the abstract domain combinators. We
  // just return an impossible access path.
  AccessPath()
      : m_kind(AccessPathKind::Unknown),
        m_parameter(std::numeric_limits<size_t>::max()),
        m_field(nullptr) {}

  /*
   * Returns an empty access path.
   */
  AccessPath(AccessPathKind kind, size_t parameter)
      : m_kind(kind), m_parameter(parameter), m_field(nullptr) {}

  AccessPath(AccessPathKind kind,
             size_t parameter,
             const std::vector<DexMethodRef*>& getters)
      : m_kind(kind),
        m_parameter(parameter),
        m_getters(getters),
        m_field(nullptr) {
    always_assert_log(kind != AccessPathKind::FinalField,
                      "Must specify a field ref");
  }

  AccessPath(AccessPathKind kind,
             size_t parameter,
             DexField* field,
             const std::vector<DexMethodRef*>& getters)
      : m_kind(kind),
        m_parameter(parameter),
        m_getters(getters),
        m_field(field) {
    if (kind == AccessPathKind::FinalField) {
      always_assert_log(field != nullptr, "Must specify a field.");
      always_assert_log((field->get_access() & ACC_FINAL) == ACC_FINAL,
                        "Field should be final!");
    } else {
      always_assert_log(field == nullptr, "Field not relevant for kind.");
    }
  }

  AccessPathKind kind() const { return m_kind; }

  size_t parameter() const { return m_parameter; }

  std::vector<DexMethodRef*> getters() const { return m_getters; }

  DexField* field() const { return m_field; }

  std::string to_string() const;

 private:
  AccessPathKind m_kind;
  size_t m_parameter;
  std::vector<DexMethodRef*> m_getters;
  // Optional members only applicable to some AccessPathKinds.
  DexField* m_field;

  friend class isa_impl::AbstractAccessPath;
};

/*
 * Holds the register to access path mappings for a block's entry state and exit
 * state.
 */
using BindingSnapshot = std::unordered_map<uint32_t, AccessPath>;

struct BlockStateSnapshot {
  BindingSnapshot entry_state_bindings;
  BindingSnapshot exit_state_bindings;
};

// To enable the use of boost::hash.
size_t hash_value(const AccessPath& path);

bool operator==(const AccessPath& x, const AccessPath& y);
bool operator!=(const AccessPath& x, const AccessPath& y);

std::ostream& operator<<(std::ostream& o, const AccessPath& path);

class ImmutableSubcomponentAnalyzer final {
 public:
  // If we don't declare a destructor for this class, a default destructor will
  // be generated by the compiler, which requires a complete definition of
  // isa_impl::Analyzer, thus causing a compilation error. Note that the
  // destructor's definition must be located after the definition of
  // isa_impl::Analyzer.
  ~ImmutableSubcomponentAnalyzer();

  /*
   * The user-provided predicate is used to decide whether a method referenced
   * in an invoke-virtual operation is a getter for an immutable structure.
   */
  ImmutableSubcomponentAnalyzer(
      DexMethod* dex_method,
      std::function<bool(DexMethodRef*)> is_immutable_getter);

  /*
   * Returns the access path to a subcomponent of an immutable structure (if
   * any) referenced by the register at the given instruction. Note that if the
   * instruction overwrites the register, the access path returned is the value
   * held by the register *before* that instruction is executed.
   */
  boost::optional<AccessPath> get_access_path(size_t reg,
                                              IRInstruction* insn) const;

  /*
   * If the given access path has been computed before and exists in the
   * instruction's entry state, returns the registers which store the path.
   */
  std::set<size_t> find_access_path_registers(IRInstruction* insn,
                                              const AccessPath& path) const;

  std::unordered_map<cfg::BlockId, BlockStateSnapshot>
  get_block_state_snapshot() const;

 private:
  std::unique_ptr<isa_impl::Analyzer> m_analyzer;
};
