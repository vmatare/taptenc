/** \file
 * Datastructures to represent clock constraints.
 *
 * \author (2019) Tarik Viehmann
 */

#include "constraints.h"
#include <cassert>
#include <iostream>
#include <limits>
#include <memory>

using namespace taptenc;

clock::clock(::std::string arg_id) : id(arg_id) {}

// Comparison Utils

::std::string computils::toString(const ComparisonOp comp) {
  switch (comp) {
  case LTE:
    return " &lt;= ";
    break;
  case LT:
    return " &lt; ";
    break;
  case GT:
    return " &gt; ";
    break;
  case GTE:
    return " &gt;= ";
    break;
  case EQ:
    return " == ";
    break;
  case NEQ:
    return " != ";
    break;
  default:
    return "unknown_op";
    break;
  }
}

ComparisonOp computils::reverseOp(ComparisonOp op) {
  if (op == ComparisonOp::LT)
    return ComparisonOp::GT;
  if (op == ComparisonOp::GT)
    return ComparisonOp::LT;
  if (op == ComparisonOp::LTE)
    return ComparisonOp::GTE;
  if (op == ComparisonOp::GTE)
    return ComparisonOp::LTE;
  if (op == ComparisonOp::EQ)
    return ComparisonOp::EQ;
  if (op == ComparisonOp::NEQ)
    return ComparisonOp::NEQ;
  std::cout << "reverseOp: unknown op:" << toString(op) << std::endl;
  return op;
}

ComparisonOp computils::inverseOp(ComparisonOp op) {
  if (op == ComparisonOp::LT)
    return ComparisonOp::GTE;
  if (op == ComparisonOp::GT)
    return ComparisonOp::LTE;
  if (op == ComparisonOp::LTE)
    return ComparisonOp::GT;
  if (op == ComparisonOp::GTE)
    return ComparisonOp::LT;
  if (op == ComparisonOp::EQ)
    return ComparisonOp::NEQ;
  if (op == ComparisonOp::NEQ)
    return ComparisonOp::EQ;
  std::cout << "inverseOp: unknown op:" << toString(op) << std::endl;
  return op;
}

// Constraints

// TrueCC
trueCC::trueCC() { type = CCType::TRUE; }

::std::string trueCC::toString() const { return "1"; }

::std::unique_ptr<ClockConstraint> trueCC::createCopy() const {
  return std::make_unique<TrueCC>(TrueCC());
}

// ConjunctionCC
conjunctionCC::conjunctionCC(const ClockConstraint &first,
                             const ClockConstraint &second) {
  ::std::unique_ptr<ClockConstraint> first_copy = first.createCopy();
  ::std::unique_ptr<ClockConstraint> second_copy = second.createCopy();
  content.first = std::move(first_copy);
  content.second = std::move(second_copy);
  type = CCType::CONJUNCTION;
}

::std::string conjunctionCC::toString() const {
  return content.first->toString() + " &amp;&amp; " +
         content.second->toString();
}

::std::unique_ptr<ClockConstraint> conjunctionCC::createCopy() const {
  return std::make_unique<ConjunctionCC>(
      ConjunctionCC(*content.first, *content.second));
}

// ComparisonCC
comparisonCC::comparisonCC(::std::shared_ptr<Clock> arg_clock,
                           ComparisonOp arg_comp, timepoint arg_constant)
    : clock(arg_clock), comp(arg_comp), constant(arg_constant) {
  type = CCType::SIMPLE_BOUND;
}

::std::unique_ptr<ClockConstraint> comparisonCC::createCopy() const {
  ComparisonCC res(clock, comp, constant);
  return std::make_unique<ComparisonCC>(res);
}

::std::string comparisonCC::toString() const {
  return clock->id + computils::toString(comp) + std::to_string(constant);
}

// DifferenceCC
differenceCC::differenceCC(::std::shared_ptr<Clock> arg_minuend,
                           ::std::shared_ptr<Clock> arg_subtrahend,
                           ComparisonOp arg_comp, timepoint arg_difference)
    : minuend(arg_minuend), subtrahend(arg_subtrahend), comp(arg_comp),
      difference(arg_difference) {
  type = CCType::DIFFERENCE;
}

::std::unique_ptr<ClockConstraint> DifferenceCC::createCopy() const {
  return std::make_unique<DifferenceCC>(
      DifferenceCC(minuend, subtrahend, comp, difference));
}

::std::string DifferenceCC::toString() const {
  return minuend.get()->id + " - " + subtrahend.get()->id +
         computils::toString(comp) + std::to_string(difference);
}

// Bounds

bounds::bounds(timepoint l, timepoint u) {
  l_op = ComparisonOp::LTE;
  r_op = u == ::std::numeric_limits<timepoint>::max() ? ComparisonOp::LT
                                                      : ComparisonOp::LTE;
  lower_bound = l;
  upper_bound = u;
}

bounds::bounds(timepoint l, timepoint u, ComparisonOp arg_l_op,
               ComparisonOp arg_r_op) {
  assert(arg_l_op == ComparisonOp::LT || arg_l_op == ComparisonOp::LTE);
  assert(arg_r_op == ComparisonOp::LT || arg_r_op == ComparisonOp::LTE);
  assert(arg_r_op == ComparisonOp::LT ||
         upper_bound != std::numeric_limits<timepoint>::max());
  l_op = arg_l_op;
  r_op = arg_r_op;
  lower_bound = l;
  upper_bound = u;
}

// Plan Action
planAction::planAction(::std::string arg_name, const Bounds &arg_duration)
    : name(arg_name), duration(arg_duration),
      delay_tolerance(0, std::numeric_limits<timepoint>::max()) {}
