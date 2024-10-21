extern "C" {
// clang-format off
#include <postgres.h>
#include <fmgr.h>
// clang-format on
#include <nodes/nodes.h>
}

#include <iostream>

#define MAGIC_ENUM_RANGE_MAX 1024
#include <magic_enum/magic_enum_all.hpp>

int main(int argc, char **argv) {
  std::cout << R"code(template <NodeTag Tag> struct NodeType {
using type = Node;
};


)code" << std::endl;

  magic_enum::enum_for_each<NodeTag>([](NodeTag val) {
    auto name = magic_enum::enum_name(val);
    if (name == "T_Invalid" || name == "T_IntList" || name == "T_OidList" || name == "T_XidList" ||
        name == "T_AllocSetContext" || name == "T_GenerationContext" || name == "T_SlabContext" ||
        name == "T_BumpContext") {
      return;
    }
    auto code = std::format(R"code(
template <> struct NodeType<{0}> {{
                         using type = {1};
                }};
)code",
                            name, name.substr(2));
    std::cout << code << std::endl;
  });
}