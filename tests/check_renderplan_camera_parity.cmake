cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED CHRONON3D_ROOT OR NOT DEFINED MOTION_SOURCE_DIR)
    message(FATAL_ERROR "CHRONON3D_ROOT and MOTION_SOURCE_DIR are required")
endif()

set(_schema_path "${CHRONON3D_ROOT}/schemas/json/chronon.render-plan.v2.schema.json")
set(_plan_layer_types "${CHRONON3D_ROOT}/include/chronon3d/render_plan/render_plan_layer_types.hpp")
set(_decoder "${CHRONON3D_ROOT}/src/render_plan/render_plan_decoder_support.cpp")
set(_fingerprint "${CHRONON3D_ROOT}/src/render_plan/fingerprint_values.cpp")
set(_camera_adapter "${CHRONON3D_ROOT}/src/render_plan/render_plan_camera_adapter.hpp")
set(_motion_emitter "${MOTION_SOURCE_DIR}/src/chrononmotion/motion/RenderPlanEmission.cpp")
set(_motion_lowering_camera "${MOTION_SOURCE_DIR}/src/chrononmotion/motion/RenderPlanLoweringCamera.cpp")
set(_motion_lowering_samples "${MOTION_SOURCE_DIR}/src/chrononmotion/motion/RenderPlanLoweringSamples.cpp")
set(_motion_validation "${MOTION_SOURCE_DIR}/src/chrononmotion/motion/RenderPlanEmissionValidationBounds.cpp")

foreach(_path IN ITEMS "${_schema_path}" "${_plan_layer_types}" "${_decoder}" "${_fingerprint}"
        "${_camera_adapter}" "${_motion_emitter}" "${_motion_lowering_camera}"
        "${_motion_lowering_samples}" "${_motion_validation}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "RenderPlan camera parity input does not exist: ${_path}")
    endif()
endforeach()

file(READ "${_schema_path}" _schema)
file(READ "${_plan_layer_types}" _plan)
file(READ "${_decoder}" _decoder_text)
file(READ "${_fingerprint}" _fingerprint_text)
file(READ "${_camera_adapter}" _adapter_text)
file(READ "${_motion_emitter}" _emitter_text)
file(READ "${_motion_lowering_camera}" _lowering_camera_text)
file(READ "${_motion_lowering_samples}" _lowering_samples_text)
file(READ "${_motion_validation}" _motion_validation_text)

function(require_source_token label source token)
    string(FIND "${source}" "${token}" _found)
    if(_found EQUAL -1)
        message(FATAL_ERROR "RenderPlan camera contract failure: ${label} is missing '${token}'")
    endif()
endfunction()

function(require_absent_token label source token)
    string(FIND "${source}" "${token}" _found)
    if(NOT _found EQUAL -1)
        message(FATAL_ERROR "RenderPlan camera contract failure: ${label} still contains '${token}'")
    endif()
endfunction()

# One typed authoring authority for each lens value.
foreach(_token IN ITEMS "float f_stop" "int iris_blades" "float dof_highlight_gain")
    require_source_token("CameraPlan" "${_plan}" "${_token}")
endforeach()
foreach(_token IN ITEMS "dof_f_stop" "dof_iris_blades" "dof_aperture")
    require_absent_token("CameraPlan" "${_plan}" "${_token}")
endforeach()

# The wire reader may normalize the deprecated nested spelling, but producers
# write only the canonical camera-level fields.
foreach(_token IN ITEMS "camera.f_stop = value.value(\"f_stop\"" "camera.iris_blades = value.value(\"iris_blades\""
        "raw_dof.contains(\"f_stop\")" "raw_dof.contains(\"iris_blades\")"
        "camera.f_stop and camera.dof.f_stop must agree" "camera.iris_blades and camera.dof.iris_blades must agree")
    require_source_token("C3D camera decoder" "${_decoder_text}" "${_token}")
endforeach()
foreach(_token IN ITEMS "camera.f_stop" "camera.iris_blades")
    require_source_token("C3D camera fingerprint" "${_fingerprint_text}" "${_token}")
endforeach()
require_source_token("C3D camera adapter" "${_adapter_text}" "source.f_stop")
require_source_token("C3D camera adapter" "${_adapter_text}" "source.iris_blades")
require_source_token("C3D camera adapter" "${_adapter_text}" "camera.lens.f_stop = authored_f_stop")

require_source_token("Motion camera lowering" "${_lowering_camera_text}" "camera.f_stop = pose.fStop")
require_source_token("Motion camera lowering" "${_lowering_camera_text}" "camera.iris_blades = pose.irisBlades")
require_source_token("Motion camera sample JSON" "${_lowering_camera_text}" "value[\"f_stop\"] = camera.f_stop")
require_source_token("Motion camera sample JSON" "${_lowering_camera_text}" "value[\"iris_blades\"] = camera.iris_blades")
require_absent_token("Motion camera sample JSON" "${_lowering_camera_text}" "value[\"dof\"][\"f_stop\"]")
require_absent_token("Motion camera sample JSON" "${_lowering_camera_text}" "value[\"dof\"][\"iris_blades\"]")

# The streaming JSON writer has exactly one occurrence of each key: the
# camera-level canonical value. Nested fields are decoder-only aliases.
foreach(_field IN ITEMS "f_stop" "iris_blades")
    string(REGEX MATCHALL "writer.key\\(\\\"${_field}\\\"\\)" _wire_writes "${_emitter_text}")
    list(LENGTH _wire_writes _wire_write_count)
    if(NOT _wire_write_count EQUAL 1)
        message(FATAL_ERROR "Motion must emit camera.${_field} exactly once; got ${_wire_write_count} writes")
    endif()
endforeach()

# All three producer/consumer entry points call the same range and capability
# admission function; none keeps a second hand-maintained camera range table.
require_source_token("Motion lowering validation" "${_lowering_samples_text}" "camera_contract_problem(*plan.camera)")
require_source_token("Motion emission validation" "${_motion_validation_text}" "camera_contract_problem(camera)")
require_source_token("C3D camera decoder" "${_decoder_text}" "camera_contract_problem(camera)")
foreach(_source IN ITEMS "${_lowering_samples_text}" "${_motion_validation_text}")
    foreach(_duplicate IN ITEMS "camera.fov_deg <" "camera.near <=" "camera.dof_max_blur <" "camera.f_stop <")
        require_absent_token("shared camera validation" "${_source}" "${_duplicate}")
    endforeach()
endforeach()

message(STATUS "RenderPlan camera contract verified: one typed lens authority, decoder-only wire aliases, shared admission")
