# Rock Hero Local Conan Recipes

This directory is a Conan 2 `local-recipes-index`. Its layout follows the ConanCenter index shape:

```text
recipes/<package>/config.yml
recipes/<package>/all/conanfile.py
recipes/<package>/all/conandata.yml
recipes/<package>/all/test_package/
```

`cmake/RockHeroConanProvider.cmake` registers this directory as the
`rock_hero_local_recipes` remote for the active project `CONAN_HOME` before delegating to the shared
Conan provider from `project-config`.

Keep recipes here close to ConanCenter style so packages can be moved upstream later with minimal
rewriting.
