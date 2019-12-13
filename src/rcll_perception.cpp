#include "constants.h"
#include "encoders.h"
#include "filter.h"
#include "plan_ordered_tls.h"
#include "platform_model_generator.h"
#include "printer.h"
#include "timed_automata.h"
#include "uppaal_calls.h"
#include "utap_trace_parser.h"
#include "utap_xml_parser.h"
#include "vis_info.h"
#include <algorithm>
#include <assert.h>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdlib.h>
#include <string>
#include <time.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace taptenc;
using namespace std;

void append_prefix_to_states(vector<State> &arg_states, string prefix) {
  for (auto it = arg_states.begin(); it != arg_states.end(); ++it) {
    it->id += prefix;
  }
}

Automaton generateSyncPlanAutomaton(
    const vector<string> &arg_plan,
    const unordered_map<string, vector<string>> &arg_constraint_activations,
    string arg_name) {
  vector<pair<int, vector<string>>> syncs_to_add;
  vector<string> full_plan;
  transform(arg_plan.begin(), arg_plan.end(), back_inserter(full_plan),
            [](const string pa) -> string { return "alpha" + pa; });
  for (auto it = arg_plan.begin(); it != arg_plan.end(); ++it) {
    auto activate = arg_constraint_activations.find(*it);
    if (activate != arg_constraint_activations.end()) {
      syncs_to_add.push_back(
          make_pair(it - arg_plan.begin(), activate->second));
    }
  }
  int offset = 0;
  for (auto it = syncs_to_add.begin(); it != syncs_to_add.end(); ++it) {
    full_plan.insert(full_plan.begin() + offset + it->first, it->second.begin(),
                     it->second.end());
    offset += it->second.size();
  }
  full_plan.push_back(constants::END_PA);
  full_plan.insert(full_plan.begin(), constants::START_PA);
  std::shared_ptr<Clock> clock_ptr = std::make_shared<Clock>("cpa");
  vector<State> plan_states;
  for (auto it = full_plan.begin(); it != full_plan.end(); ++it) {
    bool urgent = false;
    if (it->substr(0, 5) != "alpha") {
      urgent = true;
    }
    if (*it == constants::END_PA || *it == constants::START_PA) {
      plan_states.push_back(
          State(*it + constants::PA_SEP + to_string(it - full_plan.begin()),
                TrueCC(), urgent));
    } else {
      plan_states.push_back(
          State(*it + constants::PA_SEP + to_string(it - full_plan.begin()),
                ComparisonCC(clock_ptr, ComparisonOp::LT, 60), urgent));
    }
  }
  vector<Transition> plan_transitions;
  int i = 0;
  for (auto it = plan_states.begin() + 1; it < plan_states.end(); ++it) {
    string sync_op = "";
    std::unique_ptr<ClockConstraint> guard;
    update_t update = {};
    auto prev_state = (it - 1);
    if (prev_state->id.substr(0, 5) != "alpha") {
      sync_op = Filter::getPrefix(prev_state->id, constants::PA_SEP);
      guard = std::make_unique<TrueCC>();
    } else {
      update.insert(clock_ptr);
      guard = std::make_unique<ComparisonCC>(clock_ptr, ComparisonOp::GT, 30);
    }
    plan_transitions.push_back(Transition(
        prev_state->id, it->id, it->id, *guard.get(), update, sync_op, false));
    i++;
  }
  Automaton res = Automaton(plan_states, plan_transitions, arg_name, false);
  res.clocks.insert(clock_ptr);
  return res;
}

DirectEncoder createDirectEncoding(
    AutomataSystem &direct_system, const vector<PlanAction> plan,
    const vector<unique_ptr<EncICInfo>> &constraints, int plan_index = 1) {
  DirectEncoder enc(direct_system, plan);
  for (const auto &gamma : constraints) {
    for (auto pa = direct_system.instances[plan_index].first.states.begin();
         pa != direct_system.instances[plan_index].first.states.end(); ++pa) {
      string pa_op = pa->id;
      if (pa->id != constants::START_PA && pa->id != constants::END_PA) {
        pa_op = Filter::getPrefix(pa->id, constants::PA_SEP);
      } else {
        continue;
      }
      auto plan_action_it =
          std::find_if(plan.begin(), plan.end(), [pa_op](const auto &act) {
            return act.name.toString() == pa_op;
          });
      auto pa_trigger = std::find_if(
          gamma->activations.begin(), gamma->activations.end(),
          [pa_op, plan_action_it](const ActionName &s) {
            return s.ground(plan_action_it->name.args).toString() ==
                   plan_action_it->name.toString();
          });
      auto is_active = gamma->activations.end() != pa_trigger;
      if (is_active) {
        switch (gamma->type) {
        case ICType::Future: {
          std::cout << "start Future " << pa->id << std::endl;
          UnaryInfo *info = dynamic_cast<UnaryInfo *>(gamma.get());
          enc.encodeFuture(direct_system, pa->id, *info);
        } break;
        case ICType::Until: {
          std::cout << "start Until " << pa->id << std::endl;
          BinaryInfo *info = dynamic_cast<BinaryInfo *>(gamma.get());
          enc.encodeUntil(direct_system, pa->id, *info);
        } break;
        case ICType::Since: {
          std::cout << "start Since " << pa->id << std::endl;
          BinaryInfo *info = dynamic_cast<BinaryInfo *>(gamma.get());
          enc.encodeSince(direct_system, pa->id, *info);
        } break;
        case ICType::Past: {
          std::cout << "start Past " << pa->id << std::endl;
          UnaryInfo *info = dynamic_cast<UnaryInfo *>(gamma.get());
          enc.encodePast(direct_system, pa->id, *info);
        } break;
        case ICType::NoOp: {
          std::cout << "start NoOp " << pa->id << std::endl;
          UnaryInfo *info = dynamic_cast<UnaryInfo *>(gamma.get());
          enc.encodeNoOp(direct_system, info->specs.targets, pa->id);
        } break;
        case ICType::Invariant: {
          std::cout << "start Invariant " << pa->id << std::endl;
          UnaryInfo *info = dynamic_cast<UnaryInfo *>(gamma.get());
          enc.encodeInvariant(direct_system, info->specs.targets, pa->id);
        } break;
        case ICType::UntilChain: {
          ChainInfo *info = dynamic_cast<ChainInfo *>(gamma.get());
          for (auto epa = pa + 1;
               epa != direct_system.instances[plan_index].first.states.end();
               ++epa) {
            string epa_op = Filter::getPrefix(epa->id, constants::PA_SEP);
            auto eplan_action_it = std::find_if(
                plan.begin(), plan.end(), [epa_op](const auto &act) {
                  return act.name.toString() == epa_op;
                });
            if (eplan_action_it == plan.end()) {
              std::cout << "cannot find eplan_action_it" << std::endl;
              break;
            }

            auto epa_trigger = std::find_if(
                info->activations_end.begin(), info->activations_end.end(),
                [epa_op, eplan_action_it](const auto &act) {
                  return act.ground(eplan_action_it->name.args).toString() ==
                         eplan_action_it->name.toString();
                });
            bool match_found = info->activations_end.end() != epa_trigger;
            if (match_found) {
              if (epa_trigger->args.size() != pa_trigger->args.size()) {
                std::cout << "wrong argument length" << std::endl;
                // they only really match if they have the same number of args
                // this is not true in the general case, but for benchmarks
                // it is sufficient
                break;
              }
              bool missmatch = false;
              for (unsigned long i = 0; i < epa_trigger->args.size(); i++) {
                if (epa_trigger->args[i][0] == constants::VAR_PREFIX) {
                  for (unsigned long j = 0; j < pa_trigger->args.size(); j++) {
                    if (pa_trigger->args[j] == epa_trigger->args[i] &&
                        eplan_action_it->name.args[i] !=
                            plan_action_it->name.args[j]) {
                      missmatch = true;
                      break;
                    }
                  }
                }
              }
              if (!missmatch) {
                std::cout << "start until chain " << pa->id << " " << epa->id
                          << std::endl;
                enc.encodeUntilChain(direct_system, *info, pa->id, epa->id);
                break;
              } else {
              }
            }
            bool is_tightest =
                gamma->activations.end() ==
                std::find_if(
                    gamma->activations.begin(), gamma->activations.end(),
                    [eplan_action_it](const ActionName &s) {
                      return s.ground(eplan_action_it->name.args).toString() ==
                             eplan_action_it->name.toString();
                    });
            if (!is_tightest) {
              break;
            }
          }
        } break;
        default:
          cout << "error: no support yet for type " << gamma->type << endl;
        }
      }
    }
  }
  return enc;
}

ModularEncoder
createModularEncoding(AutomataSystem &system, const AutomataGlobals g,
                      unordered_map<string, vector<State>> &targets, Bounds b) {
  ModularEncoder enc(system);
  for (const auto chan : g.channels) {
    auto search = targets.find(chan.name);
    if (search != targets.end()) {
      if (chan.name.substr(0, 5) == "snoop") {
        enc.encodeNoOp(system, search->second, chan.name);
      }
      if (chan.name.substr(0, 5) == "spast") {
        enc.encodePast(system, search->second, chan.name, b);
      }
      if (chan.name.substr(0, 8) == "sfinally") {
        enc.encodeFuture(system, search->second, chan.name, b);
      }
    }
  }
  return enc;
}

// CompactEncoder
// createCompactEncoding(AutomataSystem &system, const AutomataGlobals g,
//                       unordered_map<string, vector<State>> &targets, Bounds
//                       b) {
//   CompactEncoder enc;
//   for (const auto chan : g.channels) {
//     auto search = targets.find(chan.name);
//     if (search != targets.end()) {
//       if (chan.name.substr(0, 5) == "snoop") {
//         enc.encodeNoOp(system, search->second, chan.name);
//       }
//       if (chan.name.substr(0, 5) == "spast") {
//         enc.encodePast(system, search->second, chan.name, b);
//       }
//       if (chan.name.substr(0, 8) == "sfinally") {
//         enc.encodeFuture(system, search->second, chan.name, b);
//       }
//     }
//   }
//   return enc;
// }

int main() {
  if (uppaalcalls::getEnvVar("VERIFYTA_DIR") == "") {
    cout << "ERROR: VERIFYTA_DIR not set!" << endl;
    return -1;
  }
  Bounds full_bounds(0, numeric_limits<int>::max());
  Bounds no_bounds(0, numeric_limits<int>::max());
  Bounds goto_bounds(15, 45);
  Bounds pick_bounds(13, 18);
  Bounds discard_bounds(3, 6);
  Bounds end_bounds(0, 5);
  vector<PlanAction> plan{
      // goto CS-I
      PlanAction("goto", goto_bounds), PlanAction("endgoto", end_bounds),
      // pick from shelf
      PlanAction("pick", pick_bounds), PlanAction("endpick", end_bounds),
      // put on belt
      PlanAction("put", pick_bounds), PlanAction("endput", end_bounds),
      // goto CS-O
      PlanAction("goto", goto_bounds), PlanAction("endgoto", end_bounds),
      // pick waste
      PlanAction("pick", pick_bounds), PlanAction("endpick", end_bounds),
      // discard
      PlanAction("discard", discard_bounds),
      PlanAction("enddiscard", end_bounds),
      //      // goto BS
      PlanAction("goto", goto_bounds), PlanAction("endgoto", end_bounds),
      // pick base
      PlanAction("pick", pick_bounds), PlanAction("endpick", end_bounds),
      // goto RS-I
      PlanAction("goto", goto_bounds), PlanAction("endgoto", end_bounds),
      // put on belt
      PlanAction("put", pick_bounds), PlanAction("endput", end_bounds),
      // goto RS-O
      PlanAction("goto", goto_bounds), PlanAction("endgoto", end_bounds),
      // pick base + ring 1
      PlanAction("pick", pick_bounds), PlanAction("endpick", end_bounds),
      // goto RS-I
      PlanAction("goto", goto_bounds), PlanAction("endgoto", end_bounds),
      // put on belt
      PlanAction("put", pick_bounds), PlanAction("endput", end_bounds),
      // goto RS-O
      PlanAction("goto", goto_bounds), PlanAction("endgoto", end_bounds),
      // pick base + ring 1 + ring 2
      PlanAction("pick", pick_bounds), PlanAction("endpick", end_bounds),
      // goto RS-I
      PlanAction("goto", goto_bounds), PlanAction("endgoto", end_bounds),
      // put on belt
      PlanAction("put", pick_bounds), PlanAction("endput", end_bounds),
      // goto RS-O
      PlanAction("goto", goto_bounds), PlanAction("endgoto", end_bounds),
      // pick base + ring 1 + ring 2 + ring 3
      PlanAction("pick", pick_bounds), PlanAction("endpick", end_bounds),
      // goto CS-I
      PlanAction("goto", goto_bounds), PlanAction("endgoto", end_bounds),
      // put on belt
      PlanAction("put", pick_bounds), PlanAction("endput", end_bounds),
      // goto CS-O
      PlanAction("goto", goto_bounds), PlanAction("endgoto", end_bounds),
      // pick C§
      PlanAction("pick", pick_bounds), PlanAction("endpick", end_bounds),
      // goto DS
      PlanAction("goto", goto_bounds), PlanAction("endgoto", end_bounds),
      // put on belt
      PlanAction("put", pick_bounds), PlanAction("endput", end_bounds),
      PlanAction("last", end_bounds)}; //,
  unordered_set<string> pa_names;
  transform(plan.begin(), plan.end(),
            insert_iterator<unordered_set<string>>(pa_names, pa_names.begin()),
            [](const PlanAction &pa) -> string { return pa.name; });
  AutomataGlobals glob;
  XMLPrinter printer;
  // --------------- Perception ------------------------------------
  Automaton perception_ta = benchmarkgenerator::generatePerceptionTA();
  AutomataSystem base_system;
  base_system.instances.push_back(make_pair(perception_ta, ""));
  DirectEncoder enc1 =
      createDirectEncoding(base_system, plan,
                           benchmarkgenerator::generatePerceptionConstraints(
                               perception_ta, pa_names));
  // --------------- Communication ---------------------------------
  Automaton comm_ta = benchmarkgenerator::generateCommTA();
  AutomataSystem comm_system;
  comm_system.instances.push_back(make_pair(comm_ta, ""));
  DirectEncoder enc2 = createDirectEncoding(
      comm_system, plan, benchmarkgenerator::generateCommConstraints(comm_ta));
  // --------------- Calibration ----------------------------------
  // Automaton calib_ta = benchmarkgenerator::generateCalibrationTA();
  AutomataSystem calib_system =
      utapxmlparser::readXMLSystem("platform_models/calibration.xml");
  Automaton calib_ta = calib_system.instances[0].first;
  // calib_system.instances.push_back(make_pair(calib_ta, ""));
  DirectEncoder enc3 = createDirectEncoding(
      calib_system, plan,
      benchmarkgenerator::generateCalibrationConstraints(calib_ta));
  // --------------- Positioning ----------------------------------
  AutomataSystem pos_system =
      utapxmlparser::readXMLSystem("platform_models/position.xml");
  Automaton pos_ta = pos_system.instances[0].first;
  pos_system.instances.push_back(make_pair(pos_ta, ""));
  DirectEncoder pos_enc = createDirectEncoding(
      pos_system, plan,
      benchmarkgenerator::generatePositionConstraints(pos_ta));
  // --------------- Merge ----------------------------------------
  AutomataSystem merged_system;
  merged_system.globals.clocks.insert(comm_system.globals.clocks.begin(),
                                      comm_system.globals.clocks.end());
  merged_system.globals.clocks.insert(base_system.globals.clocks.begin(),
                                      base_system.globals.clocks.end());
  merged_system.globals.clocks.insert(calib_system.globals.clocks.begin(),
                                      calib_system.globals.clocks.end());
  merged_system.instances = base_system.instances;
  DirectEncoder enc4 = enc1.mergeEncodings(enc2);
  enc4 = enc4.mergeEncodings(enc3);
  enc4 = enc4.mergeEncodings(pos_enc);
  // --------------- Print XMLs -----------------------------------
  SystemVisInfo direct_system_vis_info;
  AutomataSystem direct_system =
      enc1.createFinalSystem(base_system, direct_system_vis_info);
  printer.print(direct_system, direct_system_vis_info, "perception_direct.xml");
  SystemVisInfo comm_system_vis_info;
  AutomataSystem direct_system2 =
      enc2.createFinalSystem(comm_system, comm_system_vis_info);
  printer.print(direct_system2, comm_system_vis_info, "comm_direct.xml");
  SystemVisInfo calib_system_vis_info;
  AutomataSystem direct_system3 =
      enc3.createFinalSystem(calib_system, calib_system_vis_info);
  printer.print(direct_system3, calib_system_vis_info, "calib_direct.xml");
  SystemVisInfo merged_system_vis_info;
  SystemVisInfo pos_system_vis_info;
  AutomataSystem pos_system2 =
      pos_enc.createFinalSystem(pos_system, pos_system_vis_info);
  printer.print(pos_system2, pos_system_vis_info, "pos_direct.xml");
  AutomataSystem direct_system4 =
      enc4.createFinalSystem(merged_system, merged_system_vis_info);
  printer.print(direct_system4, merged_system_vis_info, "merged_direct.xml");
  Automaton product_ta =
      PlanOrderedTLs::productTA(perception_ta, comm_ta, "product", true);
  product_ta = PlanOrderedTLs::productTA(product_ta, calib_ta, "product", true);
  product_ta = PlanOrderedTLs::productTA(product_ta, pos_ta, "product", true);
  auto t1 = std::chrono::high_resolution_clock::now();
  uppaalcalls::solve("merged_direct");
  auto t2 = std::chrono::high_resolution_clock::now();
  UTAPTraceParser trace_parser(direct_system4);
  trace_parser.parseTraceInfo("merged_direct.trace");
  auto res = trace_parser.getTimedTrace(
      product_ta, base_system.instances[enc3.getPlanTAIndex()].first);
  auto res3 = trace_parser.applyDelay(1, 37);
  auto res4 = trace_parser.applyDelay(4, 3);
  auto res2 = trace_parser.applyDelay(37, 9);
  for (size_t i = 0; i < res.size(); i++) {
    auto tp = res[i];
    auto tp2 = res2[i];
    std::string lb = (tp.first.global_clock.first.second) ? "(" : "[";
    std::string ub = (tp.first.global_clock.second.second) ? "(" : "]";
    std::string ub_delay = (tp.first.max_delay.second) ? ")" : "]";
    std::cout << lb << tp.first.global_clock.first.first << ", "
              << tp.first.global_clock.second.first << ub << "(+ "
              << tp.first.max_delay.first << ub_delay << ":"
              << "\t\t\t";
    lb = (tp2.first.global_clock.first.second) ? "(" : "[";
    ub = (tp2.first.global_clock.second.second) ? "(" : "]";
    ub_delay = (tp2.first.max_delay.second) ? ")" : "]";
    std::cout << lb << tp2.first.global_clock.first.first << ", "
              << tp2.first.global_clock.second.first << ub << "(+ "
              << tp2.first.max_delay.first << ub_delay << std::endl;
    for (size_t j = 0; j < tp.second.size(); j++) {
      auto ac = tp.second[j];
      auto ac2 = tp2.second[j];
      std::cout << "\t" << ac << std::endl;
    }
  }
  std::cout << "duration in seconds, that it took to solve" << std::endl;
  auto duration =
      std::chrono::duration_cast<std::chrono::seconds>(t2 - t1).count();

  std::cout << duration << std::endl;
  return 0;
}
