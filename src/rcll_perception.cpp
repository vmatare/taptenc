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
    AutomataSystem &direct_system, const vector<PlanAction> &plan,
    const vector<unique_ptr<EncICInfo>> &constraints, int plan_index = 1) {
  DirectEncoder enc(direct_system, plan);
  for (const auto &gamma : constraints) {
    for (auto pa = direct_system.instances[plan_index].first.states.begin();
         pa != direct_system.instances[plan_index].first.states.end(); ++pa) {
      string pa_op = pa->id;
      if (pa->id != constants::START_PA && pa->id != constants::END_PA) {
        pa_op = Filter::getPrefix(pa->id, constants::PA_SEP);
      } else {
        // std::cout << pa->id << std::endl;
        // std::cout << "skip" << std::endl;
        continue;
      }
      auto plan_action_it =
          std::find_if(plan.begin(), plan.end(), [pa_op](const auto &act) {
            return act.name.toString() == pa_op;
          });
      auto pa_trigger = std::find_if(
          gamma->activations.begin(), gamma->activations.end(),
          [pa_op, plan_action_it](const ActionName &s) {
            // cout << "\t" << s.ground(plan_action_it->name.args).toString() <<
            // endl << "\t" <<
            //        plan_action_it->name.toString() << endl;
            return s.ground(plan_action_it->name.args).toString() ==
                   plan_action_it->name.toString();
          });
      auto is_active = gamma->activations.end() != pa_trigger;
      // if(!is_active) {
      //   std::cout << "not active" << std::endl;
      //   cin.get();
      // }
      if (is_active) {
        switch (gamma->type) {
        case ICType::Future: {
          // std::cout << "start Future " << pa->id << std::endl;
          UnaryInfo *info = dynamic_cast<UnaryInfo *>(gamma.get());
          enc.encodeFuture(direct_system, pa->id, *info);
        } break;
        case ICType::Until: {
          // std::cout << "start Until " << pa->id << std::endl;
          BinaryInfo *info = dynamic_cast<BinaryInfo *>(gamma.get());
          enc.encodeUntil(direct_system, pa->id, *info);
        } break;
        case ICType::Since: {
          // std::cout << "start Since " << pa->id << std::endl;
          BinaryInfo *info = dynamic_cast<BinaryInfo *>(gamma.get());
          enc.encodeSince(direct_system, pa->id, *info);
        } break;
        case ICType::Past: {
          // std::cout << "start Past " << pa->id << std::endl;
          UnaryInfo *info = dynamic_cast<UnaryInfo *>(gamma.get());
          enc.encodePast(direct_system, pa->id, *info);
        } break;
        case ICType::NoOp: {
          // std::cout << "start NoOp " << pa->id << std::endl;
          UnaryInfo *info = dynamic_cast<UnaryInfo *>(gamma.get());
          enc.encodeNoOp(direct_system, info->specs.targets, pa->id);
        } break;
        case ICType::Invariant: {
          // std::cout << "start Invariant " << pa->id << std::endl;
          UnaryInfo *info = dynamic_cast<UnaryInfo *>(gamma.get());
          enc.encodeInvariant(direct_system, info->specs.targets, pa->id);
        } break;
        case ICType::UntilChain: {
          // cout << "enter until chain" << endl;
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
              // std::cout << "cannot find eplan_action_it: " << epa_op <<
              // std::endl; cin.get();
              break;
            }

            auto epa_trigger = std::find_if(
                info->activations_end.begin(), info->activations_end.end(),
                [epa_op, eplan_action_it](const auto &act) {
                  // cout << "\t" <<
                  // act.ground(eplan_action_it->name.args).toString() << endl
                  // << "\t" << eplan_action_it->name.toString() << endl;
                  return act.ground(eplan_action_it->name.args).toString() ==
                         eplan_action_it->name.toString();
                });
            bool match_found = info->activations_end.end() != epa_trigger;
            // if(!match_found) {
            //    std::cout << "no match found " << pa_op << " vs " << epa_op<<
            //    std::endl; cin.get();
            // }
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
                      // cout << "missmatch found: " << pa_trigger->args[j] << "
                      // "
                      //      << epa_trigger->args[i] << std::endl;
                      // cout << eplan_action_it->name.args[i] << " vs "
                      //      << plan_action_it->name.args[j] << std::endl;
                      // cout << pa_trigger->toString() << " vs "
                      //      << epa_trigger->toString() << std::endl;
                      cin.get();
                      missmatch = true;
                      break;
                    }
                  }
                }
              }
              if (!missmatch) {
                // std::cout << "start until chain " << pa->id << " " << epa->id
                //           << std::endl;
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
              // std::cout << pa_op << " followed by " << epa_op << std::endl;
              break;
            }
          }
          // std::cout << "done looking for matches of " << pa_op << " of "
          //           << info->name << std::endl;
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

std::string idToMachineStr(int i) {
  if (i == -1)
    return "start";
  if (i == 0)
    return "bs";
  if (i == 1)
    return "cs1";
  if (i == 2)
    return "cs2";
  if (i == 3)
    return "rs1";
  if (i == 4)
    return "rs2";
  if (i == 5)
    return "ds";
  return "nomachine";
}

Bounds addBounds(const Bounds &a, const Bounds &b) {
  return Bounds(safeAddition(a.lower_bound, b.lower_bound),
                safeAddition(a.upper_bound, b.upper_bound),
                a.l_op == b.l_op ? a.l_op : ComparisonOp::LT,
                a.r_op == b.r_op ? a.r_op : ComparisonOp::LT);
}

Bounds addGrasp(::std::vector<PlanAction> &plan, std::string grasp,
                std::string obj, int m, const bounds abs_bounds) {
  Bounds grasp_bounds(15, 20);
  Bounds end_bounds(0, 30);
  Bounds res_abs_bounds = abs_bounds;
  plan.push_back(
      PlanAction(ActionName("start" + grasp, {obj, idToMachineStr(m)}),
                 res_abs_bounds, grasp_bounds));
  res_abs_bounds = addBounds(res_abs_bounds, grasp_bounds);
  plan.push_back(PlanAction(ActionName("end" + grasp, {obj, idToMachineStr(m)}),
                            res_abs_bounds, end_bounds));
  res_abs_bounds = addBounds(res_abs_bounds, end_bounds);
  return res_abs_bounds;
}
Bounds addGoto(::std::vector<PlanAction> &plan, int curr_pos, int dest_pos,
               const bounds abs_bounds) {
  Bounds goto_bounds(30, 45);
  Bounds end_bounds(0, 30);
  Bounds res_abs_bounds = abs_bounds;
  // std::cout << res_abs_bounds.lower_bound << "," <<
  // res_abs_bounds.upper_bound
  //           << std::endl;
  if (curr_pos != dest_pos) {
    plan.push_back(
        PlanAction(ActionName("startgoto", {idToMachineStr(curr_pos),
                                            idToMachineStr(dest_pos)}),
                   res_abs_bounds, goto_bounds));
    res_abs_bounds = addBounds(res_abs_bounds, goto_bounds);
    // std::cout << res_abs_bounds.lower_bound << "," <<
    // res_abs_bounds.upper_bound
    //           << std::endl;
    plan.push_back(PlanAction(ActionName("endgoto", {idToMachineStr(curr_pos),
                                                     idToMachineStr(dest_pos)}),
                              res_abs_bounds, end_bounds));
    res_abs_bounds = addBounds(res_abs_bounds, end_bounds);
    // std::cout << res_abs_bounds.lower_bound << "," <<
    // res_abs_bounds.upper_bound
    //           << std::endl;
  }
  return res_abs_bounds;
}

::std::vector<PlanAction> generatePlan(long unsigned int plan_length) {
  // std::cout << "enter plan gen " << std::endl;
  Bounds instant_bounds(0, 0);
  Bounds no_bounds(0, numeric_limits<int>::max());
  Bounds goto_bounds(30, 45);
  Bounds grasp_bounds(15, 20);
  Bounds end_bounds(0, 30);
  /*
   * Encode MPS via numbers:
   *  - -1  = START
   *  - 0   = BS
   *  - 1,2 = CS
   *  - 3,4 = RS
   *  - 5   = DS
   *
   */
  ::std::vector<PlanAction> plan;
  Bounds abs_bounds(0, 30);
  plan.push_back(PlanAction(ActionName("startplan", {"arg0", "arg1"}),
                            abs_bounds, instant_bounds));
  int cc_count = 0;
  int tr_count = 0;
  int wp_count = 0;
  int curr_pos = -1;
  for (int k = 0; k < 10; k++) {
    if (plan.size() > plan_length) {
      break;
    }
    int cs = rand() % 2 + 1;
    int ds = 5;
    int num_rings = rand() % 4;
    // std::cout << "cs: " << cs << " num_rings: " << num_rings << std::endl;
    std::vector<int> rs_ring_count(5, 0);
    std::vector<int> req_rs;
    std::vector<int> req_pay;
    for (int i = 1; i <= num_rings; i++) {
      req_rs.push_back(rand() % 2 + 3);
      req_pay.push_back(rand() % 3);
      // std::cout << "ring " << i << " req " << *(req_pay.end() - 1) << " from
      // "
      //           << *(req_rs.end() - 1) << std::endl;
    }
    int curr_step = 0;
    int req_steps = num_rings + 2;
    bool cs_buffered = false;
    std::vector<bool> occupied(6, false);
    bool abort = false;
    bool full_game = false;
    while (curr_step != req_steps) {
      if (plan.size() > plan_length) {
        break;
      }
      // if (abs_bounds.lower_bound > 1200) {
      //   full_game = true;
      //   break;
      // }
      int op_cs = rand() % 2;
      if ((!(cs_buffered && !occupied[cs])) &&
          (op_cs == 1 || curr_step == num_rings)) {
        if (cs_buffered) {
          int feed_rs = rand() % 2 + 3;
          rs_ring_count[feed_rs]++;
          if (rs_ring_count[feed_rs] == 4) {
            abort = true;
            break;
          }
          // feed cc
          abs_bounds = addGoto(plan, curr_pos, cs, abs_bounds);
          abs_bounds = addGrasp(plan, "pick", "cc" + std::to_string(cc_count),
                                cs, abs_bounds);
          abs_bounds = addGoto(plan, cs, feed_rs, abs_bounds);
          abs_bounds = addGrasp(plan, "pay", "cc" + std::to_string(cc_count),
                                feed_rs, abs_bounds);
          // std::cout << "feed cc to " << feed_rs << " from " << curr_pos
          //           << " to " << feed_rs << std::endl;
          curr_pos = feed_rs;
          occupied[cs] = false;
          cc_count++;
        } else {
          cs_buffered = true;
          occupied[cs] = true;
          abs_bounds = addGoto(plan, curr_pos, cs, abs_bounds);
          abs_bounds =
              addGrasp(plan, "getshelf", "cc" + std::to_string(cc_count), cs,
                       abs_bounds);
          abs_bounds = addGrasp(plan, "put", "cc" + std::to_string(cc_count),
                                cs, abs_bounds);
          // buffer cap
          // std::cout << "buffer cap" << std::endl;
          curr_pos = cs;
        }
      }
      if (op_cs == 1) {
        // std::cout << "hat" << std::endl;
        continue;
      }
      if (op_cs == 0 && curr_step < num_rings) {
        if (req_pay[curr_step] <= rs_ring_count[req_rs[curr_step]]) {
          rs_ring_count[req_rs[curr_step]] -= req_pay[curr_step];
          // mount ring
          int prev = -1;
          if (curr_step > 0) {
            prev = req_rs[curr_step - 1];
          }
          abs_bounds = addGoto(plan, curr_pos, prev, abs_bounds);
          abs_bounds = addGrasp(plan, "pick", "wp" + std::to_string(wp_count),
                                prev, abs_bounds);
          abs_bounds = addGoto(plan, prev, req_rs[curr_step], abs_bounds);
          abs_bounds = addGrasp(plan, "put", "wp" + std::to_string(wp_count),
                                req_rs[curr_step], abs_bounds);
          // std::cout << "mount ring on " << req_rs[curr_step] << std::endl;
          curr_pos = req_rs[curr_step];
          curr_step++;
        } else {
          int source_mat = rand() % 3;
          // get material
          std::string get = "pick";
          if (source_mat > 0) {
            get = "getshelf";
          }
          abs_bounds = addGoto(plan, curr_pos, source_mat, abs_bounds);
          abs_bounds = addGrasp(plan, get, "tr" + std::to_string(tr_count),
                                source_mat, abs_bounds);
          abs_bounds = addGoto(plan, source_mat, req_rs[curr_step], abs_bounds);
          abs_bounds = addGrasp(plan, "pay", "tr" + std::to_string(tr_count),
                                req_rs[curr_step], abs_bounds);
          tr_count++;
          curr_pos = source_mat;
          rs_ring_count[req_rs[curr_step]]++;
          // std::cout << "get material from " << source_mat << " to feed in "
          //           << req_rs[curr_step] << std::endl;
        }
      }
      if (curr_step == num_rings && cs_buffered && !occupied[cs]) {
        // std::cout << "finalize product" << curr_step << std::endl;
        int source = 0;
        if (curr_step > 0) {
          source = req_rs[curr_step - 1];
        }
        abs_bounds = addGoto(plan, curr_pos, source, abs_bounds);
        abs_bounds = addGrasp(plan, "pick", "wp" + std::to_string(wp_count),
                              source, abs_bounds);
        abs_bounds = addGoto(plan, source, cs, abs_bounds);
        abs_bounds = addGrasp(plan, "put", "wp" + std::to_string(wp_count), cs,
                              abs_bounds);
        abs_bounds = addGrasp(plan, "pick", "wp" + std::to_string(wp_count), cs,
                              abs_bounds);
        abs_bounds = addGoto(plan, cs, ds, abs_bounds);
        abs_bounds = addGrasp(plan, "put", "wp" + std::to_string(wp_count), ds,
                              abs_bounds);
        curr_pos = ds;
        // std::cout << "finalize product done" << std::endl;
        // finalize
        curr_step++;
        curr_step++;
      }
    }
    if (abort) {
      // std::cout << "aborted" << std::endl;
      return generatePlan(plan_length);
    }
    if (full_game) {
      // std::cout << "full_game" << std::endl;
      break;
    }
    wp_count++;
  }
  while (plan.size() >= plan_length) {
    plan.pop_back();
  }
  plan.push_back(PlanAction(ActionName("endplan", {"arg0", "arg1"}), abs_bounds,
                            instant_bounds));
  // for (auto &p : plan) {
  //   std::cout << p.name.toString() << " "
  //             << computils::toString(p.absolute_time.l_op)
  //             << p.absolute_time.lower_bound << ","
  //             << p.absolute_time.upper_bound
  //             << computils::toString(p.absolute_time.r_op);
  //   std::cout << " " << computils::toString(p.duration.l_op)
  //             << p.duration.lower_bound << "," << p.duration.upper_bound
  //             << computils::toString(p.duration.r_op) << std::endl;
  // }
  // cin.get();
  return plan;
}
int main(int argc, char **argv) {
  /* initialize random seed: */
  srand(time(NULL));
  if (uppaalcalls::getEnvVar("VERIFYTA_DIR") == "") {
    cout << "ERROR: VERIFYTA_DIR not set!" << endl;
    return -1;
  }
  int jay = 0;
  int num_runs_per_category = 2;
  int plan_length = 50;
  if (argc > 2) {
    for (int i = 0; i < argc; ++i) {
      if (i == 0) {
        jay = stoi(string(argv[i + 1]));
      }
      if (i == 1) {
        num_runs_per_category = stoi(string(argv[i + 1]));
      }
      if (i == 2) {
        plan_length = stoi(string(argv[i + 1]));
      }
    }
  }
  cout << "component: " << jay << " over " << num_runs_per_category
       << " of plans with length " << plan_length << std::endl;
  // int step_size=10;
  // int start_val = 10;
  // int end_val = 10;
  // Init the Automata:
  vector<string> system_names({"sys_perc", "sys_calib", "sys_comm_rs1",
                               "sys_comm_rs2", "sys_comm_cs1", "sys_comm_cs2"});
  vector<Automaton> platform_tas({benchmarkgenerator::generatePerceptionTA(),
                                  benchmarkgenerator::generateCalibrationTA(),
                                  benchmarkgenerator::generateCommTA("rs1"),
                                  benchmarkgenerator::generateCommTA("rs2"),
                                  benchmarkgenerator::generateCommTA("cs1"),
                                  benchmarkgenerator::generateCommTA("cs2")});
  //   bar.insert(bar.end(),
  //     std::make_move_iterator(baz.begin()),
  //     std::make_move_iterator(baz.end())
  // );
  vector<vector<unique_ptr<EncICInfo>>> platform_constraints;
  platform_constraints.emplace_back(
      benchmarkgenerator::generatePerceptionConstraints(platform_tas[0]));
  platform_constraints.emplace_back(
      benchmarkgenerator::generateCalibrationConstraints(platform_tas[1]));
  platform_constraints.emplace_back(
      benchmarkgenerator::generateCommConstraints(platform_tas[2], "rs1"));
  platform_constraints.emplace_back(
      benchmarkgenerator::generateCommConstraints(platform_tas[3], "rs2"));
  platform_constraints.emplace_back(
      benchmarkgenerator::generateCommConstraints(platform_tas[4], "cs1"));
  platform_constraints.emplace_back(
      benchmarkgenerator::generateCommConstraints(platform_tas[5], "cs2"));
  XMLPrinter printer;
  vector<uppaalcalls::timedelta> time_observed;
  for (int k = 0; k < num_runs_per_category; k++) {
    vector<PlanAction> plan = generatePlan(plan_length);
    AutomataSystem merged_system;
  DirectEncoder merge_enc;
  DirectEncoder first_enc;
  Automaton product_ta =
      PlanOrderedTLs::productTA(platform_tas[0], platform_tas[1], "product", true);
  for (int j = 0; j < 6; j++) {
    // cin.get();
    // unordered_set<string> pa_names;
    // transform(plan.begin(), plan.end(),
    //         insert_iterator<unordered_set<string>>(pa_names,
    //         pa_names.begin()),
    //         [](const PlanAction &pa) -> string { return pa.name.toString();
    //         });
    auto t1 = std::chrono::high_resolution_clock::now();
    AutomataSystem base_system;
    base_system.instances.push_back(make_pair(platform_tas[j], ""));
    DirectEncoder curr_encoder = createDirectEncoding(
        base_system, plan, platform_constraints[j]);
    SystemVisInfo direct_system_vis_info;
    SystemVisInfo merged_system_vis_info;
    AutomataSystem direct_system = curr_encoder.createFinalSystem(
        base_system, direct_system_vis_info);
    merged_system.globals.clocks.insert(direct_system.globals.clocks.begin(),
                                      direct_system.globals.clocks.end());
    printer.print(direct_system, direct_system_vis_info,
                  system_names[j] + "_" + to_string(k) + ".xml");
    cout << system_names[j] << "_" << k << " num states:" << direct_system.instances[0].first.states.size()<< std::endl;
    auto t2 = std::chrono::high_resolution_clock::now();
    vector<uppaalcalls::timedelta> uppaal_time = uppaalcalls::solve(system_names[j] + "_" + to_string(k));
    time_observed.insert(time_observed.end(),uppaal_time.begin(),uppaal_time.end());
    time_observed.push_back(
        std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1));
    auto t3 = std::chrono::high_resolution_clock::now();
    UTAPTraceParser trace_parser(direct_system);
    trace_parser.parseTraceInfo(system_names[j] + "_" + to_string(k) +
                                ".trace");
    auto res = trace_parser.getTimedTrace(
        platform_tas[j], base_system
                             .instances[curr_encoder.getPlanTAIndex()]
                             .first);
    auto t4 = std::chrono::high_resolution_clock::now();
    time_observed.push_back(
        std::chrono::duration_cast<std::chrono::milliseconds>(t4 - t3));
    if(j>0 && j < 2) {
    auto merge_t1 = std::chrono::high_resolution_clock::now();
      if(j>1) {
  product_ta = PlanOrderedTLs::productTA(product_ta, platform_tas[j], "product", true);
      }
      if(j==1) {
         merge_enc = first_enc.mergeEncodings(curr_encoder);
      } else {
         merge_enc = merge_enc.mergeEncodings(curr_encoder);
      }
    AutomataSystem final_merged_system = merge_enc.createFinalSystem(
        merged_system, merged_system_vis_info);
    printer.print(final_merged_system, merged_system_vis_info,
                  "merged_to_" + to_string(j) + "_" + to_string(k) + ".xml");
    auto merge_t2 = std::chrono::high_resolution_clock::now();
    cout << "merged_to_" + to_string(j) << "_" << k << " num states:" << final_merged_system.instances[0].first.states.size()<< std::endl;
    uppaal_time = uppaalcalls::solve("merged_to_" + to_string(j) + "_" + to_string(k));
    time_observed.insert(time_observed.end(),uppaal_time.begin(),uppaal_time.end());
    time_observed.push_back(
        std::chrono::duration_cast<std::chrono::milliseconds>(merge_t2-merge_t1));
    auto merge_t3 = std::chrono::high_resolution_clock::now();
    trace_parser = UTAPTraceParser(final_merged_system);
    trace_parser.parseTraceInfo("merged_to_" + to_string(j) + "_" + to_string(k) +
                                ".trace");
    res = trace_parser.getTimedTrace(
        product_ta, base_system
                             .instances[curr_encoder.getPlanTAIndex()]
                             .first);
    auto merge_t4 = std::chrono::high_resolution_clock::now();
    time_observed.push_back(
        std::chrono::duration_cast<std::chrono::milliseconds>(merge_t4-merge_t3));
    } else {
      merged_system.instances = base_system.instances;
      first_enc = curr_encoder.copy();
    }


  cout << "time: " << j  << " of " << k << std::endl;
  for (const auto &tdel : time_observed) {
    int next_instance_counter = 0;
    if(next_instance_counter == 5) {
      std::cout << std::endl;
      next_instance_counter = 0;
    }
    std::cout << tdel.count() << " ";
    next_instance_counter++;
  }
  cout << endl <<  "end time: " << j << " of " << k << std::endl;
  }
  }
  // AutomataGlobals glob;
  // --------------- Perception ------------------------------------
  // AutomataSystem base_system;
  // base_system.instances.push_back(make_pair(perception_ta, ""));
  // DirectEncoder enc1 =
  //     createDirectEncoding(base_system, plan,
  //                          benchmarkgenerator::generatePerceptionConstraints(
  //                              perception_ta, pa_names));
  // // --------------- Communication ---------------------------------
  // Automaton comm_ta = benchmarkgenerator::generateCommTA("rs1");
  // AutomataSystem comm_system;
  // comm_system.instances.push_back(make_pair(comm_ta, ""));
  // DirectEncoder enc2 = createDirectEncoding(
  //     comm_system, plan,
  //     benchmarkgenerator::generateCommConstraints(comm_ta, "rs1"));
  // --------------- Calibration ----------------------------------
  // Automaton calib_ta = benchmarkgenerator::generateCalibrationTA();
  // Automaton calib_ta = benchmarkgenerator::generateCalibrationTA();
  // AutomataSystem calib_system;
  // calib_system.instances.push_back(make_pair(calib_ta, ""));
  // AutomataSystem calib_system =
  //     utapxmlparser::readXMLSystem("platform_models/calibration.xml");
  // Automaton calib_ta = calib_system.instances[0].first;
  // calib_system.instances.push_back(make_pair(calib_ta, ""));
  // DirectEncoder enc3 = createDirectEncoding(
  //     calib_system, plan,
  //     benchmarkgenerator::generateCalibrationConstraints(calib_ta));
  // // --------------- Positioning ----------------------------------
  // AutomataSystem pos_system =
  //     utapxmlparser::readXMLSystem("platform_models/position.xml");
  // Automaton pos_ta = pos_system.instances[0].first;
  // pos_system.instances.push_back(make_pair(pos_ta, ""));
  // DirectEncoder pos_enc = createDirectEncoding(
  //     pos_system, plan,
  //     benchmarkgenerator::generatePositionConstraints(pos_ta));
  // // --------------- Merge ----------------------------------------
  // AutomataSystem merged_system;
  // merged_system.globals.clocks.insert(comm_system.globals.clocks.begin(),
  //                                     comm_system.globals.clocks.end());
  // merged_system.globals.clocks.insert(base_system.globals.clocks.begin(),
  //                                     base_system.globals.clocks.end());
  // merged_system.globals.clocks.insert(calib_system.globals.clocks.begin(),
  //                                     calib_system.globals.clocks.end());
  // merged_system.instances = base_system.instances;
  // DirectEncoder enc4 = enc1.mergeEncodings(enc3);
  //    enc4 = enc4.mergeEncodings(enc2);
  // enc4 = enc4.mergeEncodings(pos_enc);
  // // --------------- Print XMLs -----------------------------------
  // SystemVisInfo direct_system_vis_info;
  // AutomataSystem direct_system =
  //     enc1.createFinalSystem(base_system, direct_system_vis_info);
  // printer.print(direct_system, direct_system_vis_info,
  // "perception_direct.xml"); SystemVisInfo comm_system_vis_info;
  // AutomataSystem direct_system2 =
  //     enc2.createFinalSystem(comm_system, comm_system_vis_info);
  // printer.print(direct_system2, comm_system_vis_info, "comm_direct.xml");
  // SystemVisInfo calib_system_vis_info;
  // AutomataSystem direct_system3 =
  //     enc3.createFinalSystem(calib_system, calib_system_vis_info);
  // printer.print(direct_system3, calib_system_vis_info, "calib_direct.xml");
  // SystemVisInfo merged_system_vis_info;
  // SystemVisInfo pos_system_vis_info;
  // AutomataSystem pos_system2 =
  //     pos_enc.createFinalSystem(pos_system, pos_system_vis_info);
  // printer.print(pos_system2, pos_system_vis_info, "pos_direct.xml");
  // AutomataSystem direct_system4 =
  //     enc4.createFinalSystem(merged_system, merged_system_vis_info);
  // printer.print(direct_system4, merged_system_vis_info, "merged_direct.xml");
  // Automaton product_ta =
  //     PlanOrderedTLs::productTA(perception_ta, calib_ta, "product", true);
  // product_ta = PlanOrderedTLs::productTA(product_ta, comm_ta, "product",
  //  true);
  // product_ta = PlanOrderedTLs::productTA(product_ta, pos_ta,
  // "product", true);
  // auto t1 = std::chrono::high_resolution_clock::now();
  // uppaalcalls::solve("merged_direct");
  // auto t2 = std::chrono::high_resolution_clock::now();
  // UTAPTraceParser trace_parser(direct_system4);
  // trace_parser.parseTraceInfo("merged_direct.trace");
  // auto res = trace_parser.getTimedTrace(
  //     product_ta, base_system.instances[enc3.getPlanTAIndex()].first);
  // auto res3 = trace_parser.applyDelay(1, 37);
  // auto res4 = trace_parser.applyDelay(4, 3);
  // auto res2 = trace_parser.applyDelay(37, 9);
  // for (size_t i = 0; i < res.size(); i++) {
  //   auto tp = res[i];
  //   // auto tp2 = res2[i];
  //   std::string lb = (tp.first.global_clock.first.second) ? "(" : "[";
  //   std::string ub = (tp.first.global_clock.second.second) ? "(" : "]";
  //   std::string ub_delay = (tp.first.max_delay.second) ? ")" : "]";
  //   std::cout << lb << tp.first.global_clock.first.first << ", "
  //             << tp.first.global_clock.second.first << ub << "(+ "
  //             << tp.first.max_delay.first << ub_delay << ":"
  //             << "\t\t\t";
  //   // lb = (tp2.first.global_clock.first.second) ? "(" : "[";
  //   // ub = (tp2.first.global_clock.second.second) ? "(" : "]";
  //   // ub_delay = (tp2.first.max_delay.second) ? ")" : "]";
  //   // std::cout << lb << tp2.first.global_clock.first.first << ", "
  //   //           << tp2.first.global_clock.second.first << ub << "(+ "
  //   //           << tp2.first.max_delay.first << ub_delay << std::endl;
  //   for (size_t j = 0; j < tp.second.size(); j++) {
  //     auto ac = tp.second[j];
  //     // auto ac2 = tp2.second[j];
  //     std::cout << "\t" << ac << std::endl;
  //   }
  // }
  // std::cout << "duration in seconds, that it took to solve" << std::endl;
  // auto duration =
  //     std::chrono::duration_cast<std::chrono::seconds>(t2 - t1).count();
  //
  // std::cout << duration << std::endl;
  return 0;
}
