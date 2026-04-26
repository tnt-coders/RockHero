# C++26 Migration

## Idea

Upgrade the project's C++ standard from C++23 to C++26 once the toolchain and dependency
ecosystem support it. The primary motivator is **static reflection** (P2996 and related papers),
which eliminates the forwarding boilerplate of the pimpl idiom entirely.

## Motivation: reflection-driven pimpl

With C++23, the pimpl idiom requires a choice between two styles, both with drawbacks:

- **Data-only `Impl`** — concise, but mixes Tracktion-aware logic into the outer class's `.cpp`.
- **Full forwarding `Impl`** — cleaner separation, but every method signature is written twice
  (once on the outer class, once on `Impl`), and each outer-class method is a one-line forwarder.

Neither can be automated in C++23 because automation requires iterating over `Impl`'s members at
compile time — i.e. reflection. Every workaround (macros, templates, member-function pointers,
`operator->`) leaks `Impl`'s definition into the public header, which defeats the header-hiding
purpose of pimpl.

C++26 reflection provides the missing primitive. A sketch of what it could look like:

```cpp
// thumbnail.h
namespace rock_hero::audio
{

class Thumbnail
{
public:
    Thumbnail(tracktion::Engine& engine, juce::Component& owner);
    ~Thumbnail();

    void setFile(const std::filesystem::path& file);
    bool isGeneratingProxy() const;
    float getProxyProgress() const;
    double getLength() const;
    void drawChannels(juce::Graphics& g, juce::Rectangle<int> bounds, float vz) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace rock_hero::audio
```

```cpp
// thumbnail.cpp
struct Thumbnail::Impl
{
    // Members + methods written once, here.
    void setFile(const std::filesystem::path& file);
    bool isGeneratingProxy() const;
    // ...
};

// One line generates all forwarders:
ROCK_HERO_PIMPL_FORWARD(Thumbnail, Impl);
```

The `ROCK_HERO_PIMPL_FORWARD` helper would be a consteval function using reflection to enumerate
`Impl`'s public methods and synthesize matching out-of-line definitions on the outer class. Result:
full Style 2 separation with zero forwarding boilerplate, and signatures written exactly once.

## Why not now

- **Compiler support is incomplete.** As of early 2026, reflection (P2996) is not shipping in any
  mainline release of GCC, Clang, or MSVC. It exists in an EDG implementation and a Bloomberg
  Clang fork; neither is production-ready across the three compilers Rock Hero's CI uses.
- **The C++26 standard is not yet ratified.** Expected late 2026. Until then, the reflection API
  surface can still shift.
- **Dependencies have not validated against C++26.** JUCE 8 and Tracktion Engine target C++17/20
  as their minimum. Mixing a C++26 project with C++20-era headers may work, but their maintainers
  have not blessed it, and we'd be the canary for any regressions.
- **Tooling lag.** `clang-tidy`, `clang-format`, and the CI images (`project-config/`) all need
  versions that parse C++26 syntax. Static analysis of reflection-generated code is an open
  question.
- **The current pain is low.** Rock Hero has only a handful of pimpl classes today. The
  boilerplate cost is small compared to the toolchain risk of early adoption.

## Preconditions for migration

Before bumping the project to C++26, all of the following should hold:

1. **Compiler support.** At least two of GCC, Clang, and MSVC have shipped reflection in a stable
   release, with known-good versions pinned in CI.
2. **Dependency compatibility.** Tracktion Engine and JUCE build cleanly under `-std=c++26` on all
   three supported platforms (Windows/MSVC, Linux/GCC, macOS/Clang).
3. **Tooling parity.** `clang-tidy` and `clang-format` versions in use understand C++26 syntax
   without false positives on reflection code.
4. **Conan recipe coverage.** All dependencies declared in `conanfile.txt` have profiles/options
   compatible with a C++26 compiler.
5. **Stable reflection API.** The reflection proposal has landed in the ratified standard, not a
   TS or experimental header.

## Migration plan (when preconditions are met)

1. **Update `CMAKE_CXX_STANDARD`** from `23` to `26` in the root `CMakeLists.txt`.
2. **Bump CI compiler versions** in `project-config/` to the minimums validated above.
3. **Update pre-commit and clang-tidy configuration** to the matching toolchain versions.
4. **Introduce the `ROCK_HERO_PIMPL_FORWARD` helper** (or equivalent) in a shared utility header
   inside `rock-hero-core`. Validate it on one class first (e.g. `audio::Thumbnail`).
5. **Migrate existing pimpl classes incrementally** — one per PR, verifying the generated
   signatures match the hand-written ones via compile-time checks and existing tests.
6. **Update `docs/design/architectural-principles.md`** to document the reflection-based pimpl
   pattern as the standard approach for library-boundary isolation.
7. **Update `docs/design/documentation-conventions.md`** if reflection changes how Doxygen sees the
   generated methods (it may need `\fn` annotations or similar).

## When to revisit

Check back when:

- GCC 16+ or Clang 22+ ship reflection in a stable release.
- Tracktion Engine announces C++26 support or at least C++26 compatibility testing.
- The number of pimpl classes in the codebase grows enough that forwarding boilerplate becomes a
  maintenance burden rather than a minor annoyance.

A good trigger point is whenever the next major JUCE or Tracktion release bumps its own minimum
standard — that's usually a signal the ecosystem has moved.

## Risks

- **Reflection API churn.** Even post-ratification, library-side APIs around reflection may evolve
  for a few years. Code using it may need revision.
- **Build times.** Consteval-heavy reflection code can be expensive to compile. Worth measuring
  before committing to it broadly.
- **Debuggability.** Generated forwarders may have confusing stack traces or step-through
  behavior. Verify the debugging experience on at least one platform before migrating all pimpl
  classes.
- **Cognitive load.** Reflection is a new language feature most contributors will not have seen
  before. Keep usage localized to a single well-documented helper rather than spreading it
  through the codebase.
