/*************************************************************************
 * Value-only helpers shared by Interact's timing-driven placement hosts.
 *
 * These functions define the part of the host contract Interact owns:
 * translating a Dali-decided pair count into an ACT process name, matching
 * names to declared delay sites without prefix collisions, and producing
 * deterministic evidence strings. They intentionally depend on neither ACT
 * nor Dali so their boundary semantics can be unit-tested in isolation.
 *************************************************************************/
#ifndef INTERACT_TIMING_DRIVEN_HELPERS_H
#define INTERACT_TIMING_DRIVEN_HELPERS_H

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace interact {
namespace timing_driven {

inline bool SiteMember (const std::string &name, const std::string &site)
{
  if (name == site) return true;
  if (name.compare (0, site.size (), site) != 0) return false;
  if (name.size () == site.size ()) return true;
  const char next = name[site.size ()];
  return next == '_' || next == '.' || next == '[' || next == '/';
}

inline bool SplitEndpoint (const std::string &endpoint,
                           std::string *component, std::string *pin)
{
  const size_t colon = endpoint.rfind (':');
  if (colon == std::string::npos || colon == 0
      || colon + 1 == endpoint.size ()) {
    return false;
  }
  *component = endpoint.substr (0, colon);
  *pin = endpoint.substr (colon + 1);
  return true;
}

/** Substitute one positive count into one literal {pairs} placeholder. */
inline bool FormatProcessName (const std::string &tmpl, int pairs,
                               std::string *out, std::string *error)
{
  const std::string token = "{pairs}";
  const size_t first = tmpl.find (token);
  if (first == std::string::npos) {
    *error = "process template '" + tmpl + "' has no {pairs} placeholder";
    return false;
  }
  if (tmpl.find (token, first + token.size ()) != std::string::npos) {
    *error = "process template '" + tmpl + "' repeats {pairs}";
    return false;
  }
  for (size_t i = 0; i < tmpl.size (); ++i) {
    if (i >= first && i < first + token.size ()) continue;
    if (tmpl[i] == '{' || tmpl[i] == '}') {
      *error = "process template '" + tmpl + "' has a stray brace";
      return false;
    }
  }
  if (pairs <= 0) {
    *error = "pair count " + std::to_string (pairs) + " is not positive";
    return false;
  }
  *out = tmpl.substr (0, first) + std::to_string (pairs)
      + tmpl.substr (first + token.size ());
  return true;
}

inline std::string FnvDigest (const std::vector<std::string> &items)
{
  uint64_t value = 1469598103934665603ULL;
  for (const std::string &item : items) {
    for (unsigned char c : item) {
      value ^= c;
      value *= 1099511628211ULL;
    }
    value ^= 0xff;
    value *= 1099511628211ULL;
  }
  std::ostringstream output;
  output << std::hex << std::setw (16) << std::setfill ('0') << value;
  return output.str ();
}

inline std::string JsonEscape (const std::string &value)
{
  static constexpr char kHex[] = "0123456789abcdef";
  std::ostringstream escaped;
  for (unsigned char character : value) {
    switch (character) {
      case '"': escaped << "\\\""; break;
      case '\\': escaped << "\\\\"; break;
      case '\b': escaped << "\\b"; break;
      case '\f': escaped << "\\f"; break;
      case '\n': escaped << "\\n"; break;
      case '\r': escaped << "\\r"; break;
      case '\t': escaped << "\\t"; break;
      default:
        if (character < 0x20) {
          escaped << "\\u00" << kHex[character >> 4]
                  << kHex[character & 0x0f];
        } else {
          escaped << character;
        }
    }
  }
  return escaped.str ();
}

}  // namespace timing_driven
}  // namespace interact

#endif
