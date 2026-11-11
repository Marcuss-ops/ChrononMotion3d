if(NOT DEFINED TOOL OR NOT EXISTS "${TOOL}")
    message(FATAL_ERROR "character plan tool not found: ${TOOL}")
endif()
if(NOT DEFINED OUT_DIR OR OUT_DIR STREQUAL "")
    set(OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}")
endif()
if(OUT_DIR STREQUAL "")
    set(OUT_DIR ".")
endif()
set(OUT "${OUT_DIR}/character-plan-cli.plan.json")

function(expect_failure label)
    execute_process(COMMAND "${TOOL}" ${ARGN}
                    RESULT_VARIABLE result
                    OUTPUT_VARIABLE output
                    ERROR_VARIABLE error)
    if(result EQUAL 0)
        message(FATAL_ERROR "${label}: expected a non-zero exit, got success\nstdout: ${output}")
    endif()
    if(NOT error MATCHES "chrononmotion_character_plan:")
        message(FATAL_ERROR "${label}: expected the tool error prefix, got: ${error}")
    endif()
endfunction()

function(expect_success label)
    execute_process(COMMAND "${TOOL}" ${ARGN}
                    RESULT_VARIABLE result
                    OUTPUT_VARIABLE output
                    ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${label}: expected success, got ${result}\nstdout: ${output}\nstderr: ${error}")
    endif()
endfunction()

expect_failure("no arguments")
expect_failure("unknown flag" --nope)
expect_failure("one-clip sequence" --rig missing.glb --out "${OUT}" --sequence A)
expect_failure("frames with a sequence" --rig missing.glb --out "${OUT}" --sequence A,B --frames 10)
expect_failure("clip plus sequence" --rig missing.glb --out "${OUT}" --clip A --sequence B,C)
expect_failure("unreadable rig" --rig missing.glb --out "${OUT}")

if(DEFINED RIG AND EXISTS "${RIG}")
    expect_failure("unknown clip" --rig "${RIG}" --strip-rig-materials
                   --clip __missing__ --frames 2 --out "${OUT}")
    expect_failure("invalid fov" --rig "${RIG}" --out "${OUT}" --fov 200)
    expect_failure("non-finite camera" --rig "${RIG}" --out "${OUT}" --camera nan,0,0)
    expect_failure("crossfade longer than clip" --rig "${RIG}" --strip-rig-materials
                   --sequence Idle2,Idle --crossfade 10 --fps 10 --out "${OUT}")
    if(DEFINED MESH AND EXISTS "${MESH}")
        expect_failure("wrong base topology" --rig "${RIG}" --strip-rig-materials
                       --mesh "${MESH}" --clip Idle2 --frames 2 --out "${OUT}")
    endif()
    set(FACE_MAP "${OUT_DIR}/face-map.json")
    set(FACE_CUES "${OUT_DIR}/face-cues.json")
    file(WRITE "${FACE_MAP}"
         "{\"geometry_id\":1,\"visemes\":{\"AA\":[{\"morph\":0,\"weight\":1.0}]},\"blink_morph\":1}\n")
    file(WRITE "${FACE_CUES}"
         "{\"first_frame\":0,\"last_frame\":1,\"visemes\":[{\"frame\":0,\"viseme\":\"AA\",\"weight\":1.0}]}\n")
    expect_failure("face map on a rig with no morphs" --rig "${RIG}" --strip-rig-materials
                   --clip Idle2 --frames 2 --face-map "${FACE_MAP}" --face-cues "${FACE_CUES}"
                   --out "${OUT}")
endif()

if(DEFINED RIG AND EXISTS "${RIG}")
    set(AUTO_OUT "${OUT_DIR}/character-plan-auto-texture.plan.json")
    set(AUTO_TEXTURE "${OUT_DIR}/character-plan-auto-texture.plan.png")
    file(REMOVE "${AUTO_OUT}" "${AUTO_TEXTURE}" "${AUTO_OUT}.manifest.json")
    expect_success("auto embedded texture"
                   --rig "${RIG}" --strip-rig-materials --clip Idle2 --frames 2
                   --out "${AUTO_OUT}")
    file(READ "${AUTO_OUT}" auto_plan)
    if(NOT auto_plan MATCHES "\"asset\"[ \t]*:[ \t]*\"character-plan-auto-texture.plan.png\"")
        message(FATAL_ERROR "auto texture: plan does not reference the extracted sidecar")
    endif()
    if(NOT EXISTS "${AUTO_TEXTURE}")
        message(FATAL_ERROR "auto texture: sidecar was not written at ${AUTO_TEXTURE}")
    endif()
    file(REMOVE "${AUTO_OUT}" "${AUTO_TEXTURE}" "${AUTO_OUT}.manifest.json")
endif()

if(EXISTS "${OUT}")
    message(FATAL_ERROR "a failed invocation left a partial plan at ${OUT}")
endif()
