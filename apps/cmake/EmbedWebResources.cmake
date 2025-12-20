# EmbedWebResources.cmake
# Embeds web files (HTML, CSS, JS) into a C++ header at build time.
#
# Usage in CMakeLists.txt:
#   include(cmake/EmbedWebResources.cmake)
#   embed_web_resources(
#       OUTPUT ${CMAKE_BINARY_DIR}/generated/WebResources.h
#       HTML src/server/web/garden.html
#       CSS src/server/web/garden.css
#       JS src/server/web/garden.js
#   )

function(embed_web_resources)
    cmake_parse_arguments(EMBED "" "OUTPUT;HTML;CSS;JS" "" ${ARGN})

    # Convert to absolute paths.
    get_filename_component(HTML_PATH "${EMBED_HTML}" ABSOLUTE BASE_DIR ${CMAKE_SOURCE_DIR})
    get_filename_component(CSS_PATH "${EMBED_CSS}" ABSOLUTE BASE_DIR ${CMAKE_SOURCE_DIR})
    get_filename_component(JS_PATH "${EMBED_JS}" ABSOLUTE BASE_DIR ${CMAKE_SOURCE_DIR})
    get_filename_component(OUTPUT_DIR "${EMBED_OUTPUT}" DIRECTORY)

    # Ensure output directory exists.
    file(MAKE_DIRECTORY ${OUTPUT_DIR})

    # Create the generator script.
    set(GENERATOR_SCRIPT "${CMAKE_BINARY_DIR}/generate_web_resources.cmake")
    file(WRITE ${GENERATOR_SCRIPT} "
# Read input files.
file(READ \"${HTML_PATH}\" HTML_CONTENT)
file(READ \"${CSS_PATH}\" CSS_CONTENT)
file(READ \"${JS_PATH}\" JS_CONTENT)

# Replace placeholders in HTML with actual content.
string(REPLACE \"{{GARDEN_CSS}}\" \"\${CSS_CONTENT}\" COMBINED_HTML \"\${HTML_CONTENT}\")
string(REPLACE \"{{GARDEN_JS}}\" \"\${JS_CONTENT}\" COMBINED_HTML \"\${COMBINED_HTML}\")

# Escape backslashes and quotes for C++ string literal.
string(REPLACE \"\\\\\" \"\\\\\\\\\" COMBINED_HTML \"\${COMBINED_HTML}\")
string(REPLACE \"\\\"\" \"\\\\\\\"\" COMBINED_HTML \"\${COMBINED_HTML}\")

# Convert newlines to escaped newlines for raw string.
# Actually, we'll use C++11 raw string literals which handle this better.

# Re-read for raw string approach (no escaping needed).
file(READ \"${HTML_PATH}\" HTML_CONTENT)
file(READ \"${CSS_PATH}\" CSS_CONTENT)
file(READ \"${JS_PATH}\" JS_CONTENT)
string(REPLACE \"{{GARDEN_CSS}}\" \"\${CSS_CONTENT}\" COMBINED_HTML \"\${HTML_CONTENT}\")
string(REPLACE \"{{GARDEN_JS}}\" \"\${JS_CONTENT}\" COMBINED_HTML \"\${COMBINED_HTML}\")

# Generate the header file.
file(WRITE \"${EMBED_OUTPUT}\" \"// Auto-generated file. Do not edit manually.
// Generated from: garden.html, garden.css, garden.js
// Regenerate by rebuilding the project.

#pragma once

#include <string_view>

namespace DirtSim {
namespace Server {
namespace WebResources {

inline constexpr std::string_view kGardenHtml = R\\\"WEBRESOURCE(
\${COMBINED_HTML}
)WEBRESOURCE\\\";

} // namespace WebResources
} // namespace Server
} // namespace DirtSim
\")
")

    # Add custom command to generate the header.
    add_custom_command(
        OUTPUT ${EMBED_OUTPUT}
        COMMAND ${CMAKE_COMMAND} -P ${GENERATOR_SCRIPT}
        DEPENDS ${HTML_PATH} ${CSS_PATH} ${JS_PATH}
        COMMENT "Generating embedded web resources..."
        VERBATIM
    )

    # Create a custom target for the generated header.
    add_custom_target(web_resources_generate DEPENDS ${EMBED_OUTPUT})

endfunction()
