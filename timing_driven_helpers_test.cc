/* Unit tests for Interact's value-only timing-driven host contract. */

#include <cstdio>
#include <string>
#include <vector>

#include "timing_driven_helpers.h"

namespace td = interact::timing_driven;

static int Fail (const char *message)
{
  std::fprintf (stderr, "timing-driven helper regression: %s\n", message);
  return 1;
}

int main ()
{
  std::string result;
  std::string error;
  if (!td::FormatProcessName ("plain_delay<{pairs}>", 12, &result, &error)
      || result != "plain_delay<12>") {
    return Fail ("valid process template was not translated exactly");
  }
  for (const std::string &bad : {"plain_delay<12>", "{pairs}{pairs}",
                                 "plain_delay<{other}>"}) {
    if (td::FormatProcessName (bad, 12, &result, &error))
      return Fail ("malformed process template was accepted");
  }
  if (td::FormatProcessName ("plain_delay<{pairs}>", 0, &result, &error))
    return Fail ("non-positive pair count was accepted");

  for (const std::string &member : {"dl1", "dl1_cell", "dl1.cell",
                                    "dl1[0]", "dl1/cell"}) {
    if (!td::SiteMember (member, "dl1"))
      return Fail ("token-boundary site member was rejected");
  }
  if (td::SiteMember ("dl10_cell", "dl1"))
    return Fail ("prefix-colliding site member was accepted");

  std::string component;
  std::string pin;
  if (!td::SplitEndpoint ("stage:cell:Y", &component, &pin)
      || component != "stage:cell" || pin != "Y") {
    return Fail ("endpoint was not split at its final colon");
  }
  for (const std::string &bad : {"missing", ":Y", "cell:"}) {
    if (td::SplitEndpoint (bad, &component, &pin))
      return Fail ("malformed endpoint was accepted");
  }

  const std::vector<std::string> values = {"a", "b"};
  if (td::FnvDigest (values) != td::FnvDigest (values)
      || td::FnvDigest (values) == td::FnvDigest ({"b", "a"})) {
    return Fail ("inventory digest is not deterministic and order-sensitive");
  }
  if (td::JsonEscape ("a\n\"b\\") != "a\\n\\\"b\\\\")
    return Fail ("JSON evidence escaping changed");

  std::printf ("timing-driven helper regression passed\n");
  return 0;
}
