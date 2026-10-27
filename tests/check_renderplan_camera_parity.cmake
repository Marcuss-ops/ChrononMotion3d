cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED CHRONON3D_ROOT OR NOT DEFINED MOTION_SOURCE_DIR)
    message(FATAL_ERROR "CHRONON3D_ROOT and MOTION_SOURCE_DIR are required")
endif()

set(_schema_path "${CHRONON3D_ROOT}/schemas/json/chronon.render-plan.v2.schema.json")
set(_plan_header "${CHRONON3D_ROOT}/include/chronon3d/render_plan/render_plan.hpp")
set(_plan_layer_types "${CHRONON3D_ROOT}/include/chronon3d/render_plan/render_plan_layer_types.hpp")
set(_decoder "${CHRONON3D_ROOT}/src/render_plan/render_plan_decoder_support.cpp")
set(_fingerprint "${CHRONON3D_ROOT}/src/render_plan/fingerprint_values.cpp")
set(_motion_emitter "${MOTION_SOURCE_DIR}/src/chrononmotion/motion/RenderPlanEmission.cpp")
set(_motion_lowering "${MOTION_SOURCE_DIR}/src/chrononmotion/motion/RenderPlanLowering.cpp")
set(_motion_lowering_camera "${MOTION_SOURCE_DIR}/src/chrononmotion/motion/RenderPlanLoweringCamera.cpp")
set(_motion_lowering_samples "${MOTION_SOURCE_DIR}/src/chrononmotion/motion/RenderPlanLoweringSamples.cpp")
set(_chronon_compiler "${CHRONON3D_ROOT}/src/render_plan/render_plan_compiler_animation_utils.cpp")

foreach(_path IN ITEMS
        "${_schema_path}" "${_plan_header}" "${_plan_layer_types}" "${_decoder}" "${_fingerprint}"
        "${_motion_emitter}" "${_motion_lowering}" "${_motion_lowering_camera}"
        "${_motion_lowering_samples}" "${_chronon_compiler}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "RenderPlan camera parity input does not exist: ${_path}")
    endif()
endforeach()

file(READ "${_schema_path}" _schema)
file(READ "${_plan_header}" _plan)
file(READ "${_plan_layer_types}" _plan_layer_text)
string(APPEND _plan "\n${_plan_layer_text}")
file(READ "${_decoder}" _decoder_text)
file(READ "${_fingerprint}" _fingerprint_text)
file(READ "${_motion_emitter}" _emitter_text)
file(READ "${_motion_lowering}" _lowering_text)
file(READ "${_motion_lowering_camera}" _lowering_camera_text)
file(READ "${_motion_lowering_samples}" _lowering_samples_text)
string(APPEND _lowering_text "\n${_lowering_camera_text}\n${_lowering_samples_text}")
file(READ "${_chronon_compiler}" _compiler_text)

string(JSON _dof_properties ERROR_VARIABLE _json_error
    GET "${_schema}" "$defs" "camera" "properties" "dof" "properties")
if(_json_error)
    message(FATAL_ERROR "Could not read camera.dof properties from the V2 schema: ${_json_error}")
endif()
string(JSON _field_count LENGTH "${_dof_properties}")
if(_field_count LESS 1)
    message(FATAL_ERROR "The V2 schema declares no camera DoF fields")
endif()

function(require_source_token label source token)
    string(FIND "${source}" "${token}" _found)
    if(_found EQUAL -1)
        message(FATAL_ERROR "RenderPlan camera parity failure: ${label} is missing '${token}'")
    endif()
endfunction()

require_source_token("Motion DoF emission" "${_emitter_text}" "writer.key(\"dof\")")

math(EXPR _last_index "${_field_count} - 1")
foreach(_index RANGE 0 ${_last_index})
    string(JSON _field MEMBER "${_dof_properties}" ${_index})
    set(_plan_field "dof_${_field}")

    # The contract list is derived directly from the authoritative JSON schema.
    # Each path below must explicitly consume/carry the same field.
    require_source_token("CameraPlan" "${_plan}" "${_plan_field}")
    require_source_token("RenderPlan decoder" "${_decoder_text}" "raw_dof.value(\"${_field}\"")
    require_source_token("RenderPlan fingerprint" "${_fingerprint_text}" "camera.${_plan_field}")
    require_source_token("Motion JSON writer" "${_emitter_text}" "writer.key(\"${_field}\")")
    require_source_token("Motion lowerer" "${_lowering_text}" "camera.${_plan_field}")
    require_source_token("Chronon camera lowering" "${_compiler_text}" "source.${_plan_field}")
    if(_field STREQUAL "f_stop")
        # Physical f-stop resolves through the camera lens model, then selects
        # the physical DoF path when a focal length is present.
        require_source_token("Chronon camera lowering" "${_compiler_text}" "camera.lens.f_stop = authored_f_stop")
        require_source_token("Chronon camera lowering" "${_compiler_text}" "camera.dof.use_physical_model = source.focal_length_mm > 0.0f && authored_f_stop > 0.0f")
    elseif(_field STREQUAL "enabled")
        require_source_token("Chronon camera lowering" "${_compiler_text}" "camera.dof.enabled = source.dof_enabled")
    elseif(_field STREQUAL "focus_distance")
        require_source_token("Chronon camera lowering" "${_compiler_text}" "camera.dof.focus_distance = source.dof_focus_distance")
    elseif(_field STREQUAL "aperture")
        require_source_token("Chronon camera lowering" "${_compiler_text}" "camera.dof.aperture = source.dof_aperture")
    elseif(_field STREQUAL "max_blur")
        require_source_token("Chronon camera lowering" "${_compiler_text}" "camera.dof.max_blur = source.dof_max_blur")
    elseif(_field STREQUAL "iris_blades")
        require_source_token("Chronon camera lowering" "${_compiler_text}" "camera.dof.iris_blades = static_cast<std::uint32_t>(source.dof_iris_blades)")
    elseif(_field STREQUAL "highlight_gain")
        require_source_token("Chronon camera lowering" "${_compiler_text}" "camera.dof.highlight_gain = source.dof_highlight_gain")
    endif()
endforeach()

message(STATUS "RenderPlan camera DoF parity verified for ${_field_count} schema-defined fields")
