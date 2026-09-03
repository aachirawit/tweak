# TweetNaCl

`src/backend/ed25519_verify.cpp` needs TweetNaCl to check the Ed25519 signature
on KeyAuth replies. Without it the app still builds, the build prints a warning,
and signature verification is off — the licence check then relies on TLS and a
timestamp freshness check alone, which does not stop someone who can get a root
certificate trusted on the machine from forging a reply.

## Adding it

Put these two files next to this README:

| File | Source |
| --- | --- |
| `tweetnacl.c` | <https://tweetnacl.cr.yp.to/20140427/tweetnacl.c> |
| `tweetnacl.h` | <https://tweetnacl.cr.yp.to/20140427/tweetnacl.h> |

TweetNaCl is public domain, by Daniel J. Bernstein, Wesley Janssen, Tanja Lange,
Peter Schwabe, Sjaak Smetsers and Bernard van Gastel. It is a single ~800-line C
file with no dependencies.

Then add `tweetnacl.c` to `SZK.vcxproj` under the existing thirdparty group, with
warnings relaxed the same way the ImGui entries are — it is C, not C++, and it is
not our code to keep warning-clean:

```xml
<ClCompile Include="thirdparty\tweetnacl\tweetnacl.c">
  <WarningLevel>Level3</WarningLevel>
  <TreatWarningAsError>false</TreatWarningAsError>
  <CompileAs>CompileAsC</CompileAs>
</ClCompile>
```

`ed25519_verify.cpp` finds the header through `__has_include`, so nothing else
needs changing. Rebuild, and the build warning goes away.

## Status

Vendored and working. The build no longer prints the warning, and a valid
licence key signs in with verification active — which is also what confirms
the public key in `keyauth_config.h`, since verification fails closed.

If a key that should work is ever rejected with "could not be verified", the
public key constant is wrong (KeyAuth rotated it, or the API version changed).
Fix the constant from KeyAuth's own current C++ example. Do not turn the check
off — that gives up the replay protection as well.
