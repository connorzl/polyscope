#include "polyscope/surface_input_curve_quantity.h"

#include "polyscope/gl/shaders.h"
#include "polyscope/gl/shaders/cylinder_shaders.h"
#include "polyscope/gl/shaders/surface_shaders.h"
#include "polyscope/pick.h"
#include "polyscope/file_helpers.h"
#include "polyscope/polyscope.h"

#include "imgui.h"

#include <fstream>
#include <iostream>

using namespace geometrycentral;

namespace polyscope {

using std::cout;
using std::endl;

SurfaceInputCurveQuantity::SurfaceInputCurveQuantity(std::string name, SurfaceMesh* mesh_)
    : SurfaceQuantity(name, mesh_), curve(parent->geometry) {
  curveColor = mesh_->colorManager.getNextSubColor(name);

  // Create the program
  program = new gl::GLProgram(&PASSTHRU_CYLINDER_VERT_SHADER, &CYLINDER_GEOM_SHADER, &SHINY_CYLINDER_FRAG_SHADER,
                              gl::DrawMode::Points);
}

SurfaceInputCurveQuantity::~SurfaceInputCurveQuantity() { safeDelete(program); }


void SurfaceInputCurveQuantity::draw() {

  if (!enabled) return;

  if (bufferStale) fillBuffers();

  // Set uniforms
  glm::mat4 viewMat = parent->getModelView();
  program->setUniform("u_viewMatrix", glm::value_ptr(viewMat));

  glm::mat4 projMat = view::getCameraPerspectiveMatrix();
  program->setUniform("u_projMatrix", glm::value_ptr(projMat));

  Vector3 eyePos = view::getCameraWorldPosition();
  program->setUniform("u_eye", eyePos);

  program->setUniform("u_lightCenter", state::center);
  program->setUniform("u_lightDist", 5 * state::lengthScale);
  program->setUniform("u_radius", radiusParam * state::lengthScale);
  program->setUniform("u_color", curveColor);


  program->draw();
}

void SurfaceInputCurveQuantity::fillBuffers() {

  std::vector<Vector3> pTail, pTip;

  for (CurveSegment& c : curve.getCurveSegments()) {
    pTail.push_back(c.startPosition);
    pTip.push_back(c.endPosition);
  }

  program->setAttribute("a_position_tail", pTail);
  program->setAttribute("a_position_tip", pTip);

  bufferStale = false;
}

void SurfaceInputCurveQuantity::drawUI() {
  bool enabledBefore = enabled;
  if (ImGui::TreeNode((name + " (surface curve)").c_str())) {
    ImGui::Checkbox("Enabled", &enabled);
    ImGui::SameLine();

    // == Options popup
    if (ImGui::Button("Options")) {
      ImGui::OpenPopup("OptionsPopup");
    }
    if (ImGui::BeginPopup("OptionsPopup")) {

      if (ImGui::MenuItem("Write to file")) writeToFile();

      ImGui::EndPopup();
    }
    ImGui::SameLine();

    ImGui::ColorEdit3("Color", (float*)&curveColor, ImGuiColorEditFlags_NoInputs);
    ImGui::SliderFloat("Radius", &radiusParam, 0.0, .1, "%.5f", 3.);

    if (allowEditingFromDefaultUI) {
      if (ImGui::Button("Edit")) {
        userEdit();
      }
    }

    ImGui::TreePop();
  }
}


void SurfaceInputCurveQuantity::userEdit() {

  // Make sure we can see what we're editing
  enabled = true;

  // Create a new context
  ImGuiContext* oldContext = ImGui::GetCurrentContext();
  ImGuiContext* newContext = ImGui::CreateContext(getGlobalFontAtlas());
  ImGui::SetCurrentContext(newContext);
  initializeImGUIContext();
  bool oldAlwaysPick = pick::alwaysEvaluatePick;
  pick::alwaysEvaluatePick = true;

  // Register the callback which creates the UI and does the hard work
  focusedPopupUI = std::bind(&SurfaceInputCurveQuantity::userEditCallback, this);

  // Re-enter main loop
  while (focusedPopupUI) {
    mainLoopIteration();
  }

  // Restore the old context
  pick::alwaysEvaluatePick = oldAlwaysPick;
  ImGui::SetCurrentContext(oldContext);
  ImGui::DestroyContext(newContext);
}

void SurfaceInputCurveQuantity::userEditCallback() {

  static bool showWindow = true;
  ImGui::SetNextWindowSize(ImVec2(300, 0), ImGuiCond_Once);
  ImGui::Begin(("Edit Curve [name: " + name + "]").c_str(), &showWindow);

  ImGui::PushItemWidth(300);

  // Tell em what's up
  ImGui::TextWrapped("This mode allows you to input a surface curve, which is defined to be a connected sequence of "
                     "straight lines within faces. The curve may be a closed loop, or may be open with two endpoints "
                     "in the middle of a face. No limitations are imposed on self-intersection.\n\n");
  ImGui::TextWrapped(
      "Hold CTRL and left click on the surface to draw the curve. Nothing will happen unless the clicked "
      "point is adjacent to the previous endpoint. The `Close Curve` button will close the curve if both endpoints are "
      "in the same face.");

  // Process mouse selection if the ctrl key is held, the mouse is pressed, and the mouse isn't on the ImGui window
  ImGuiIO& io = ImGui::GetIO();
  if (io.KeyCtrl && !io.WantCaptureMouse && ImGui::IsMouseDown(0)) {

    // Check what face, if any, was clicked
    FacePtr fClick;
    Vector3 bCoord;
    parent->getPickedFacePoint(fClick, bCoord);

    if (fClick != FacePtr()) {
      curve.tryExtendBack(fClick, bCoord);
      bufferStale = true;
    }
  }


  // Shrink the selection
  if (ImGui::Button("Remove last")) {
    curve.removeLastEndpoint();
    bufferStale = true;
  }

  // Close
  if (ImGui::Button("Close curve")) {
    if (curve.isClosed()) {
      polyscope::error("Curve is already closed.");
    } else {
      try {
        curve.closeCurve();
        bufferStale = true;
      } catch (std::runtime_error e) {
        polyscope::error("Error closing curve. Are both endpoints in same face?");
      }
    }
  }


  // Clear
  if (ImGui::Button("Clear")) {
    curve.clearCurve();
    bufferStale = true;
  }


  // Stop editing
  // (style makes yellow button)
  ImGui::Spacing();
  ImGui::Spacing();
  ImGui::Spacing();
  ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(1. / 7.0f, 0.6f, 0.6f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(1. / 7.0f, 0.7f, 0.7f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(1. / 7.0f, 0.8f, 0.8f));
  if (ImGui::Button("Done")) {
    focusedPopupUI = nullptr;
  }
  ImGui::PopStyleColor(3);

  ImGui::PopItemWidth();

  ImGui::End();
}


MeshEmbeddedCurve SurfaceInputCurveQuantity::getCurve() {
  return curve.copy(parent->transfer, parent->originalGeometry);
}

void SurfaceInputCurveQuantity::setCurve(MeshEmbeddedCurve& newCurve) {
  curve = newCurve.copyBack(parent->transfer, parent->geometry);
  bufferStale = true;
}


void SurfaceInputCurveQuantity::writeToFile(std::string filename) {

  if (filename == "") {
    filename = promptForFilename();
    if (filename == "") {
      return;
    }
  }

  cout << "Writing curve " << name << " to file " << filename << endl;
  std::vector<CurveSegment> segments = curve.getCurveSegments();

  std::ofstream outFile(filename);

  // Write positions and record indices
  std::vector<std::pair<size_t, size_t>> segInds;
  if (curve.isClosed()) {
    outFile << "# points: " << segments.size() << endl;

    size_t i = 0;
    for (CurveSegment& s : segments) {
      outFile << s.startPosition << endl;
      segInds.push_back(std::make_pair(i, (i + 1) % segments.size()));
      i++;
    }

  } else {
    outFile << "# points: " << (segments.size() + 1) << endl;

    size_t i = 0;
    for (CurveSegment& s : segments) {
      outFile << s.startPosition << endl;
      segInds.push_back(std::make_pair(i, i + 1));
      i++;
    }
    outFile << segments.back().endPosition << endl;
  }

  // Write indices
  outFile << "# lines: " << segInds.size() << endl;
  for (auto s : segInds) {
    outFile << s.first << "," << s.second << endl;
  }

  outFile.close();
}

} // namespace polyscope
