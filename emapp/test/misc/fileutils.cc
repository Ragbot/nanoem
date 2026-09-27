/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "../common.h"

#include "emapp/FileUtils.h"

using namespace nanoem;
using namespace test;

TEST_CASE("fileutils_canonicalize_path_separator", "[emapp][misc]")
{
    String result;
    FileUtils::canonicalizePathSeparator("", result);
    CHECK(result == String());
    FileUtils::canonicalizePathSeparator("C:\\path\\to\\windows", result);
    CHECK(result == String("C:/path/to/windows"));
    FileUtils::canonicalizePathSeparator("C:\\\\path\\\\to\\\\windows", result);
    CHECK(result == String("C:/path/to/windows"));
}

TEST_CASE("fileutils_canonicalize_path", "[emapp][misc]")
{
    CHECK(FileUtils::canonicalizePath("", "") == String());
    CHECK(FileUtils::canonicalizePath("/path/to", "file") == String("/path/to/file"));
    CHECK(FileUtils::canonicalizePath("/path/to", "../file") == String("/path/file"));
    CHECK(FileUtils::canonicalizePath("/path/to", "../../file") == String("file"));
    CHECK(FileUtils::canonicalizePath("/path/to", "../../../file") == String("file"));
    CHECK(FileUtils::canonicalizePath("/path/to", "./file") == String("/path/to/file"));
    CHECK(FileUtils::canonicalizePath("/path/to", "././file") == String("/path/to/file"));
    CHECK(FileUtils::canonicalizePath("/path/to", "./../file") == String("/path/file"));
    CHECK(FileUtils::canonicalizePath("/path/to", "./.././../file") == String("file"));
    CHECK(FileUtils::canonicalizePath("./path/to", "file") == String("path/to/file"));
    CHECK(FileUtils::canonicalizePath("./path/to", "../file") == String("path/file"));
    CHECK(FileUtils::canonicalizePath("./path/to", "../../file") == String("./file"));
    CHECK(FileUtils::canonicalizePath("./path/to", "../../../file") == String("file"));
}

TEST_CASE("fileutils_relative_path", "[emapp][misc]")
{
    CHECK(FileUtils::relativePath("", "") == String());
    CHECK(FileUtils::relativePath("/", "/") == String());
    CHECK(FileUtils::relativePath("/", "/path/to/base") == String("../../../"));
    CHECK(FileUtils::relativePath("/path/to/relative", "") == String());
    CHECK(FileUtils::relativePath("/path/to/relative", "/") == String("path/to/relative"));
    CHECK(FileUtils::relativePath("/path/to/relative", "/path/to/base") == String("../relative"));
    CHECK(FileUtils::relativePath("/relative", "/path/to/base") == String("../../../relative"));
    CHECK(FileUtils::relativePath("/path/to/relative", "/base") == String("../path/to/relative"));
    CHECK(FileUtils::relativePath("C:/", "C:/") == String());
    CHECK(FileUtils::relativePath("C:/", "C:/path/to/base") == String("../../../"));
    CHECK(FileUtils::relativePath("C:/path/to/relative", "") == String());
    CHECK(FileUtils::relativePath("C:/path/to/relative", "C:/") == String("path/to/relative"));
    CHECK(FileUtils::relativePath("D:/path/to/relative", "D:/path/to/base") == String("../relative"));
    CHECK(FileUtils::relativePath("D:/relative", "D:/path/to/base") == String("../../../relative"));
    CHECK(FileUtils::relativePath("D:/path/to/relative", "D:/base") == String("../path/to/relative"));
    CHECK(FileUtils::relativePath("D:/path/to/relative", "C:/base") == String());
}

#if !BX_PLATFORM_WINDOWS
TEST_CASE("fileutils_find_existing_file_case_insensitive", "[emapp][misc]")
{
    char path[1024], lowercase[1024], missing[1024];
    snprintf(path, sizeof(path), "%s/CaseSensitiveProbe.PNG", NANOEM_TEST_OUTPUT_PATH);
    FILE *fp = fopen(path, "wb");
    REQUIRE(fp != nullptr);
    fputs("probe", fp);
    fclose(fp);
    {
        /* exact casing: there is nothing to correct */
        const URI uri = URI::createFromFilePath(String(path));
        URI resolved;
        CHECK_FALSE(FileUtils::findExistingFileCaseInsensitive(uri, resolved));
    }
    {
        /* models authored on Windows often differ in casing only */
        snprintf(lowercase, sizeof(lowercase), "%s/casesensitiveprobe.png", NANOEM_TEST_OUTPUT_PATH);
        const URI uri = URI::createFromFilePath(String(lowercase));
        URI resolved;
        REQUIRE(FileUtils::findExistingFileCaseInsensitive(uri, resolved));
        CHECK(FileUtils::exists(resolved));
        CHECK_FALSE(String(resolved.absolutePathConstString()) == String(uri.absolutePathConstString()));
    }
    {
        snprintf(missing, sizeof(missing), "%s/NoSuchFileAnywhere.png", NANOEM_TEST_OUTPUT_PATH);
        const URI uri = URI::createFromFilePath(String(missing));
        URI resolved;
        CHECK_FALSE(FileUtils::findExistingFileCaseInsensitive(uri, resolved));
    }
    FileUtils::deleteFile(path);
}
#endif /* !BX_PLATFORM_WINDOWS */

TEST_CASE("fileutils_user_configuration_file_path", "[emapp][misc]")
{
    const String path = FileUtils::userConfigurationFilePath();
    const nanoem_rsize_t size = static_cast<nanoem_rsize_t>(path.size());
    const char *value = path.c_str();
    REQUIRE(size > 18);
    /* the path always terminates with <base>/nanoem/config.json */
    CHECK(String(value + size - 18) == String("nanoem/config.json"));
    bool hasSeparator = false;
    for (nanoem_rsize_t i = 0; i < size; i++) {
        if (value[i] == '/') {
            hasSeparator = true;
        }
    }
    CHECK(hasSeparator);
}

TEST_CASE("fileutils_create_directories", "[emapp][misc]")
{
    char buffer[1024];
    snprintf(buffer, sizeof(buffer), "%s/nanoem_test_create_directories/a/b", NANOEM_TEST_OUTPUT_PATH);
    const String path(buffer);
    REQUIRE(FileUtils::createDirectories(path));
    CHECK(FileUtils::exists(buffer));
    /* creating an existing directory must be harmless */
    CHECK(FileUtils::createDirectories(path));
}
