extern "C" {

// clang-format off
#include "../../.pg/Darwin-Debug/17.0/postgresql-17.0/src/include/postgres.h"
#include <fmgr.h>
// clang-format on
#include <access/tableam.h>
#include <access/tsmapi.h>
#include <commands/event_trigger.h>
#include <commands/trigger.h>
#include <foreign/fdwapi.h>
#include <nodes/extensible.h>
#include <nodes/nodes.h>
#include <nodes/replnodes.h>
#include <nodes/supportnodes.h>
#include <nodes/tidbitmap.h>

#include <omni/omni_v0.h>
}

static_assert(FLEXIBLE_ARRAY_MEMBER == 1);

#include <concepts>
#include <unordered_set>

#include <boost/pfr.hpp>

#define MAGIC_ENUM_RANGE_MAX 1024
#include <magic_enum/magic_enum_all.hpp>

#include "repr.hpp"

#include "omni_guard.h"
#include <pg_nodetypes.hpp>

template <typename T> struct guard {
  static void invoke(T &t, std::unordered_set<void *> &ptrset) {
    //    ereport(NOTICE, errmsg("default"));
  }
};

template <std::integral T> struct guard<T> {
  static void invoke(T &t, std::unordered_set<void *> &ptrset) {
    //    ereport(NOTICE, errmsg("integral"));
  }
};

template <typename T>
concept ClassType = std::is_class_v<T>;

#define NDEBUG

#ifdef NDEBUG
#include <cxxabi.h>

template <typename T> std::string demangle(const char *name) {
  int status = -1;
  std::unique_ptr<char, void (*)(void *)> res{abi::__cxa_demangle(name, NULL, NULL, &status),
                                              std::free};
  return (status == 0) ? res.get() : name;
}
#endif

template <ClassType T> struct guard<T> {
  static void invoke(T &t, std::unordered_set<void *> &ptrset) {

    // We don't know what that is, introspect
    auto fill = [ptrset](auto info) mutable {
      //      constexpr auto name = boost::pfr::detail::get_name<T, decltype(index)::value>();
      //      if (name == "jointree") {
      //        ereport(NOTICE, errmsg("%s", name.data()));
      //      }
      //      using Ty = decltype(field);
      //      using Ty1 = std::remove_reference_t<Ty>;
      //      Ty a0 = std::move(field);
      //      Ty1 a = std::move(field);
      //      guard<std::remove_reference_t<decltype(field)>>::invoke(std::move(field), ptrset);
      using Ty = typename decltype(info)::type;
      auto val = (Ty)info.value();
      guard<Ty>::invoke(al, ptrset);
    };
    //    boost::pfr::for_each_field(t, fill);
    static_assert(std::is_aggregate_v<T> &&
                  !(std::is_array_v<T> || librepr::has_custom_members<T> ||
                    librepr::detail::has_repr_member<T> || std::is_union_v<T>));
    librepr::Reflect<T>::visit(fill, t);
  }
};

template <typename T> struct guard<const T *> {
  static void invoke(T *&ptr, std::unordered_set<void *> &ptrset) {
    guard<std::remove_const<T>>::invoke(std::move(ptr), ptrset);
  }
};

template <typename T> struct guard<T *const> {
  static void invoke(const T *const &ptr, std::unordered_set<void *> &ptrset) {
    ereport(NOTICE, errmsg("*const"));
    // TODO: code reuse?
    if (ptr != nullptr) {
      if (ptrset.insert((void *)ptr).second) {
        T *ptr1 = (T *)ptr;
        guard<T>::invoke(*ptr1, ptrset);
      }
    }
  }
};

template <typename T> struct guard<T *> {
  static void invoke(T *&ptr, std::unordered_set<void *> &ptrset) {
    if (ptr != nullptr) {
      if (ptrset.insert(ptr).second) {
        guard<T>::invoke(*ptr, ptrset);
      }
    }
  }
};

template <> struct guard<Node> {
  static void invoke(Node &node, std::unordered_set<void *> &ptrset) {
    castNode(TableAmRoutine, &node);
    magic_enum::enum_switch(
        [ptrset, &node](auto val) mutable {
          using Ty = NodeType<val>::type;
          if constexpr (requires { Ty{}; } && !std::is_same_v<Ty, Node> &&
                        !std::is_same_v<Ty, List>) { // avoid loops
            ereport(NOTICE, errmsg("here %s", demangle<Ty>(typeid(Ty).name()).data()));
            Ty recast_node = *reinterpret_cast<Ty *>(&node);
            guard<Ty>::invoke(recast_node, ptrset);
          }
        },
        node.type);
  }
};
template <> struct guard<List *const> {
  static void invoke(List *const &list, std::unordered_set<void *> &ptrset) {
    if (list != nullptr) {
      if (ptrset.insert((void *)list).second) {
        ListCell *lc;
        switch (list->type) {
        case T_IntList:
          foreach (lc, list) {
            guard<int>::invoke(lfirst_int(lc), ptrset);
          }
          break;
        case T_OidList:
          foreach (lc, list) {
            guard<Oid>::invoke(lfirst_oid(lc), ptrset);
          }
          break;
        case T_XidList:
          foreach (lc, list) {
            guard<Oid>::invoke(lfirst_xid(lc), ptrset);
          }
          break;
        case T_List:
          foreach (lc, list) {
            Node *node = (Node *)lfirst(lc);
            guard<Node>::invoke(*node, ptrset);
          }
          break;
        default:
          break;
        }
      }
    }
  }
};

static void guard_planner(omni_hook_handle *handle, Query *parse, const char *query_string,
                          int cursorOptions, ParamListInfo boundParams) {
  std::unordered_set<void *> ptrset;
  ereport(NOTICE, errmsg("%s %d", nodeToString(parse), std::is_class_v<FromExpr>));
  guard<Query *>::invoke(parse, ptrset);
}

extern "C" {
void guard_init(const omni_handle *handle) {
  omni_hook planner_hook = {
      .type = omni_hook_planner,
      .fn = {.planner = guard_planner},
      .name = (char *)"omni_guard planner hook",
  };
  handle->register_hook(handle, &planner_hook);
}
}