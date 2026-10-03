# configure
set(NANOEM_EXECUTABLE_NAME ${PROJECT_NAME}_qt)
set(QT_SHELL_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/main.cc
                     ${CMAKE_CURRENT_SOURCE_DIR}/src/QtApplicationClient.cc
                     ${CMAKE_CURRENT_SOURCE_DIR}/src/QtApplicationMenuBuilder.cc
                     ${CMAKE_CURRENT_SOURCE_DIR}/src/QtApplicationService.cc
                     ${CMAKE_CURRENT_SOURCE_DIR}/src/QtModalDialogPresenter.cc
                     ${CMAKE_CURRENT_SOURCE_DIR}/src/QtAccessoryOutsideParentDialog.cc
                     ${CMAKE_CURRENT_SOURCE_DIR}/src/QtAccessoryPanel.cc
                     ${CMAKE_CURRENT_SOURCE_DIR}/src/QtBoneBiasDialog.cc
                     ${CMAKE_CURRENT_SOURCE_DIR}/src/QtBoneCorrectionDialog.cc
                     ${CMAKE_CURRENT_SOURCE_DIR}/src/QtBonePanel.cc
                     ${CMAKE_CURRENT_SOURCE_DIR}/src/QtCameraCorrectionDialog.cc
                     ${CMAKE_CURRENT_SOURCE_DIR}/src/QtCameraPanel.cc
                     ${CMAKE_CURRENT_SOURCE_DIR}/src/QtInterpolationCurveGraphDialog.cc
                     ${CMAKE_CURRENT_SOURCE_DIR}/src/QtInterpolationCurveWidget.cc
                     ${CMAKE_CURRENT_SOURCE_DIR}/src/QtInterpolationPanel.cc
                     ${CMAKE_CURRENT_SOURCE_DIR}/src/QtLightPanel.cc
                     ${CMAKE_CURRENT_SOURCE_DIR}/src/QtModelDrawOrderDialog.cc
                     ${CMAKE_CURRENT_SOURCE_DIR}/src/QtModelEdgeDialog.cc
                     ${CMAKE_CURRENT_SOURCE_DIR}/src/QtModelOutsideParentDialog.cc
                     ${CMAKE_CURRENT_SOURCE_DIR}/src/QtModelPanel.cc
                     ${CMAKE_CURRENT_SOURCE_DIR}/src/QtModelTransformOrderDialog.cc
                     ${CMAKE_CURRENT_SOURCE_DIR}/src/QtMorphCorrectionDialog.cc
                     ${CMAKE_CURRENT_SOURCE_DIR}/src/QtMorphPanel.cc
                     ${CMAKE_CURRENT_SOURCE_DIR}/src/QtPhysicsEngineDialog.cc
                     ${CMAKE_CURRENT_SOURCE_DIR}/src/QtPlayPanel.cc
                     ${CMAKE_CURRENT_SOURCE_DIR}/src/QtScaleAllSelectedKeyframesDialog.cc
                     ${CMAKE_CURRENT_SOURCE_DIR}/src/QtSelfShadowDialog.cc
                     ${CMAKE_CURRENT_SOURCE_DIR}/src/QtTimelinePanel.cc
                     ${CMAKE_CURRENT_SOURCE_DIR}/src/QtUIWindow.cc
                     ${CMAKE_CURRENT_SOURCE_DIR}/src/QtViewPanel.cc
                     ${CMAKE_CURRENT_SOURCE_DIR}/src/QtViewport3D.cc
                     ${CMAKE_CURRENT_SOURCE_DIR}/src/QtViewportOverlay.cc
                     ${CMAKE_CURRENT_SOURCE_DIR}/src/QtViewportParameterBox.cc
                     ${CMAKE_CURRENT_SOURCE_DIR}/src/QtViewportSettingDialog.cc
                     ${CMAKE_CURRENT_SOURCE_DIR}/src/MainWindow.cc)
add_executable(${NANOEM_EXECUTABLE_NAME} ${QT_SHELL_SOURCES})
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT ${NANOEM_EXECUTABLE_NAME})
# the project globally sets CMAKE_CXX_STANDARD 14 but Qt6 requires C++17
set_target_properties(${NANOEM_EXECUTABLE_NAME} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_include_directories(${NANOEM_EXECUTABLE_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(${NANOEM_EXECUTABLE_NAME} Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets)
nanoem_emapp_link_executable(${NANOEM_EXECUTABLE_NAME})

# plugins
get_property(_plugins GLOBAL PROPERTY NANOEM_PROPERTY_INSTALL_PLUGINS)
if(NANOEM_ENABLE_STATIC_BUNDLE_PLUGIN)
  target_link_libraries(${NANOEM_EXECUTABLE_NAME} ${_plugins})
else()
  set(_qt_shell_plugins ${_plugins})
  if(TARGET sokol_glcore33)
    list(APPEND _qt_shell_plugins sokol_glcore33)
  endif()
  if(TARGET plugin_effect)
    list(APPEND _qt_shell_plugins plugin_effect)
  endif()
  list(REMOVE_DUPLICATES _qt_shell_plugins)
  foreach(_item ${_qt_shell_plugins})
    add_dependencies(${NANOEM_EXECUTABLE_NAME} ${_item})
    add_custom_command(TARGET ${NANOEM_EXECUTABLE_NAME} POST_BUILD
                       COMMAND ${CMAKE_COMMAND} -E make_directory
                               "$<TARGET_FILE_DIR:${NANOEM_EXECUTABLE_NAME}>/plugins"
                       COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${_item}>"
                               "$<TARGET_FILE_DIR:${NANOEM_EXECUTABLE_NAME}>/plugins/$<TARGET_FILE_NAME:${_item}>"
                       VERBATIM)
  endforeach()
endif()
