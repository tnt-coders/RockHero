import os

from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
from conan.tools.files import copy, get, replace_in_file

required_conan_version = ">=2.0"


class Libebur128Conan(ConanFile):
    name = "libebur128"
    description = "Library implementing the EBU R128 loudness standard"
    license = "MIT"
    homepage = "https://github.com/jiixyj/libebur128"
    url = "https://github.com/tnt-coders/RockHero"
    topics = ("audio", "loudness", "ebu-r128", "lufs")
    package_type = "library"

    settings = "os", "arch", "compiler", "build_type"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
    }
    default_options = {
        "shared": False,
        "fPIC": True,
    }

    def config_options(self):
        if self.settings.os == "Windows":
            self.options.rm_safe("fPIC")

    def configure(self):
        if self.options.shared:
            self.options.rm_safe("fPIC")

    def layout(self):
        cmake_layout(self, src_folder="src")

    def source(self):
        get(self, **self.conan_data["sources"][self.version], strip_root=True)
        replace_in_file(
            self,
            os.path.join(self.source_folder, "CMakeLists.txt"),
            "cmake_minimum_required(VERSION 2.8.12 FATAL_ERROR)",
            "cmake_minimum_required(VERSION 3.10 FATAL_ERROR)",
        )
        replace_in_file(
            self,
            os.path.join(self.source_folder, "test", "CMakeLists.txt"),
            "cmake_minimum_required(VERSION 2.8.12)",
            "cmake_minimum_required(VERSION 3.10)",
        )

    def generate(self):
        toolchain = CMakeToolchain(self)
        toolchain.variables["BUILD_SHARED_LIBS"] = self.options.shared
        toolchain.variables["ENABLE_TESTS"] = False
        toolchain.variables["ENABLE_FUZZER"] = False
        toolchain.variables["WITH_STATIC_PIC"] = self.options.get_safe("fPIC", False)
        toolchain.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        copy(
            self,
            "COPYING",
            src=self.source_folder,
            dst=os.path.join(self.package_folder, "licenses"),
        )
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "libebur128")
        self.cpp_info.set_property("cmake_target_name", "libebur128::ebur128")
        self.cpp_info.libs = ["ebur128"]

        if self.settings.os in ["Linux", "FreeBSD"] and not self.options.shared:
            self.cpp_info.system_libs.append("m")
