if(NOT DEFINED CLI OR NOT DEFINED ROOT)
  message(FATAL_ERROR "Provide the built content CLI and its test workspace")
endif()
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef token)
set(case_root "${ROOT}/content-café with spaces-${token}")
if(EXISTS "${case_root}")
  message(FATAL_ERROR "Content CLI test directory already exists")
endif()
file(MAKE_DIRECTORY "${case_root}")
file(WRITE "${case_root}/profile.json" "{\"schema_version\":1,\"packages\":[]}")
execute_process(
  COMMAND "${CLI}" resolve "${case_root}/profile.json" --store "${case_root}/store"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0 OR NOT output MATCHES "\"ok\":true")
  message(FATAL_ERROR "Unicode content CLI arguments failed (${result}): ${output} ${error}")
endif()
file(MAKE_DIRECTORY "${case_root}/source")
file(WRITE "${case_root}/source/pack.json" [=[{
  "schema_version":3,"game_id":"mw3","id":"unicode","version":"1.0.0","files":{},
  "definitions":[
    {"kind":"world","id":"unicode:shared","behavior":"authored","data":{
      "meshes":[{"vertices":[[0,0,0],[1,0,0],[0,0,-1]],"triangles":[[0,1,2]]}]}},
    {"kind":"mission_item","id":"unicode:shared","behavior":"token-v1","data":{"name":"Marker"}}
  ]
}]=])
execute_process(
  COMMAND "${CLI}" compile "${case_root}/source" "${case_root}/output" --store "${case_root}/store"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Unicode source compilation failed (${result}): ${output} ${error}")
endif()
file(READ "${case_root}/output/pack.json" manifest)
string(JSON behavior GET "${manifest}" definitions 1 behavior)
if(NOT behavior STREQUAL "token-v1")
  message(FATAL_ERROR "World compilation changed another kind with the same stable ID")
endif()
execute_process(
  COMMAND "${CLI}" install "${case_root}/output" --store "${case_root}/store"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Unicode compiled package installation failed (${result}): ${output} ${error}")
endif()
file(REMOVE "${case_root}/profile.json")
message(STATUS "Unicode and spaced profile/store command-line paths passed")
