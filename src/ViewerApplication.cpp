#include "ViewerApplication.hpp"

#include <iostream>
#include <numeric>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/io.hpp>

#include "utils/cameras.hpp"
#include "utils/gltf.hpp"
#include "utils/images.hpp"

#include <stb_image_write.h>
#include <tiny_gltf.h>

const GLuint VERTEX_ATTRIB_POSITION_IDX = 0;
const GLuint VERTEX_ATTRIB_NORMAL_IDX = 1;
const GLuint VERTEX_ATTRIB_TEXCOORD0_IDX = 2;

void keyCallback(
    GLFWwindow *window, int key, int scancode, int action, int mods)
{
  if (key == GLFW_KEY_ESCAPE && action == GLFW_RELEASE) {
    glfwSetWindowShouldClose(window, 1);
  }
}

int ViewerApplication::run()
{
  // Loader shaders
  const auto glslProgram =
      compileProgram({m_ShadersRootPath / m_vertexShader,
          m_ShadersRootPath / m_fragmentShader});

  const auto modelViewProjMatrixLocation =
      glGetUniformLocation(glslProgram.glId(), "uModelViewProjMatrix");
  const auto modelViewMatrixLocation =
      glGetUniformLocation(glslProgram.glId(), "uModelViewMatrix");
  const auto normalMatrixLocation =
      glGetUniformLocation(glslProgram.glId(), "uNormalMatrix");

  //Light & base color
  const auto uLightDirectionLocation = glGetUniformLocation(glslProgram.glId(), "uLightDirection");
  const auto uLightIntensity = glGetUniformLocation(glslProgram.glId(), "uLightIntensity");
  const auto uBaseColorTexture = glGetUniformLocation(glslProgram.glId(), "uBaseColorTexture");
  const auto uBaseColorFactor = glGetUniformLocation(glslProgram.glId(), "uBaseColorFactor");

  //Metallic
  const auto uMetallicRoughness = glGetUniformLocation(glslProgram.glId(), "uMetallicRoughnessTexture");
  const auto uMetallicFactor = glGetUniformLocation(glslProgram.glId(), "uMetallicFactor");
  const auto uRoughnessFactor = glGetUniformLocation(glslProgram.glId(), "uRoughnessFactor");

  //Emission
  const auto uEmissiveTexture = glGetUniformLocation(glslProgram.glId(), "uEmissiveTexture");
  const auto uEmissiveFactor = glGetUniformLocation(glslProgram.glId(), "uEmissiveFactor");

  //Occlusion
  const auto uOcclusionTexture = glGetUniformLocation(glslProgram.glId(), "uOcclusionTexture");
  const auto uOcclusionStrength = glGetUniformLocation(glslProgram.glId(), "uOcclusionStrength");
  const auto uApplyOcclusion = glGetUniformLocation(glslProgram.glId(), "uApplyOcclusion");


  //Init light parameters
  glm::vec3 lightDirection(1,1,1);
  glm::vec3 lightIntensity(1,1,1);
  bool lightFromCamera = false;
  bool applyOcclusion = true;

  tinygltf::Model model;
  // Loading the glTF file
  if (!loadGltfFile(model))
    throw std::exception("Unable to load glTF model");

  //Load textures
  const auto textureObjects = createTextureObjects(model);

  //Default white texture
  float white[] = {1,1,1,1};
  GLuint whiteTexture;
  glGenTextures(1, &whiteTexture);
  glBindTexture(GL_TEXTURE_2D, whiteTexture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_FLOAT, white);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_REPEAT);
  glBindTexture(GL_TEXTURE_2D, 0);

  // Creation of Buffer Objects
  auto bufferObjects = createBufferObjects(model);

  // Creation of Vertex Array Objects
  std::vector<VaoRange> meshToVA;
  auto vertexArrayObjects = createVertexArrayObjects(model, bufferObjects, meshToVA);

  // Scene bounding box
  glm::vec3 bboxMin, bboxMax;
  computeSceneBounds(model, bboxMin, bboxMax);

  // Build projection matrix
  // Using scene bounds
  const auto diagonal = bboxMax - bboxMin;
  auto maxDist = glm::length(diagonal);
  const auto projMatrix =
      glm::perspective(70.f, float(m_nWindowWidth) / m_nWindowHeight,
          0.001f * maxDist, 1.5f * maxDist);

  // Implement a new CameraController model and use it instead. Propose the
  // choice from the GUI
  std::unique_ptr<CameraController> cameraController = 
    std::make_unique<TrackballCameraController>(
      m_GLFWHandle.window(), 0.5f * maxDist);
  if (m_hasUserCamera) {
    cameraController->setCamera(m_userCamera);
  } else {
    // Using scene bounds to compute a better default camera
    const auto center = 0.5f * (bboxMax + bboxMin);
    const auto up = glm::vec3(0 , 1 , 0);
    const auto eye = diagonal.z > 0.f ? center + diagonal : center + 2.f * glm::cross(diagonal, up);
    cameraController->setCamera(Camera(eye, center, up));
  }

  // Setup OpenGL state for rendering
  glEnable(GL_DEPTH_TEST);
  glslProgram.use();

  const auto bindMaterial = [&](const auto materialIndex) {
    if (materialIndex >= 0) {
      const auto &material = model.materials[materialIndex];
      const auto &pbrMetallicRoughness = material.pbrMetallicRoughness;
      if (uBaseColorFactor >= 0) {
        glUniform4f(uBaseColorFactor,
            (float)pbrMetallicRoughness.baseColorFactor[0],
            (float)pbrMetallicRoughness.baseColorFactor[1],
            (float)pbrMetallicRoughness.baseColorFactor[2],
            (float)pbrMetallicRoughness.baseColorFactor[3]);
      }
      
      //Set Material to white
      if (uBaseColorTexture >= 0) {
        if (uBaseColorFactor >= 0) {
          glUniform4f(uBaseColorFactor, 1, 1, 1, 1);
        }
        auto textureObject = whiteTexture;
        if (pbrMetallicRoughness.baseColorTexture.index >= 0) {
          const auto &texture =
              model.textures[pbrMetallicRoughness.baseColorTexture.index];
          if (texture.source >= 0) {
            textureObject = textureObjects[texture.source];
          }
        }
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textureObject);
        glUniform1i(uBaseColorTexture, 0);
      }

      if (uMetallicFactor >= 0) {
        glUniform1f(
            uMetallicFactor, (float)pbrMetallicRoughness.metallicFactor);
      }
      if (uRoughnessFactor >= 0) {
        glUniform1f(
            uRoughnessFactor, (float)pbrMetallicRoughness.roughnessFactor);
      }
      if (uMetallicRoughness >= 0) {
        auto textureObject = 0u;
        if (pbrMetallicRoughness.metallicRoughnessTexture.index >= 0) {
          const auto &texture =
              model.textures[pbrMetallicRoughness.metallicRoughnessTexture
                                 .index];
          if (texture.source >= 0) {
            textureObject = textureObjects[texture.source];
          }
        }
      }

      if (uEmissiveFactor >= 0) {
        glUniform3f(uEmissiveFactor, (float)material.emissiveFactor[0],
            (float)material.emissiveFactor[1],
            (float)material.emissiveFactor[2]);
      }
      if (uEmissiveTexture >= 0) {
        auto textureObject = 0u;
        if (material.emissiveTexture.index >= 0) {
          const auto &texture = model.textures[material.emissiveTexture.index];
          if (texture.source >= 0) {
            textureObject = textureObjects[texture.source];
          }
        }
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, textureObject);
        glUniform1i(uEmissiveTexture, 2);
      }

      if (uOcclusionStrength >= 0) {
        glUniform1f(
            uOcclusionStrength, (float)material.occlusionTexture.strength);
      }
      if (uOcclusionTexture >= 0) {
        auto textureObject = whiteTexture;
        if (material.occlusionTexture.index >= 0) {
          const auto &texture = model.textures[material.occlusionTexture.index];
          if (texture.source >= 0) {
            textureObject = textureObjects[texture.source];
          }
        }
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, textureObject);
        glUniform1i(uOcclusionTexture, 3);
      }

    } else {
      if (uBaseColorFactor >= 0) {
        glUniform4f(uBaseColorFactor, 1, 1, 1, 1);
      }
      if (uBaseColorTexture >= 0) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, whiteTexture);
        glUniform1i(uBaseColorTexture, 0);
      }
      if (uMetallicFactor >= 0) {
        glUniform1f(uMetallicFactor, 1.f);
      }
      if (uRoughnessFactor >= 0) {
        glUniform1f(uRoughnessFactor, 1.f);
      }
      if (uEmissiveFactor >= 0) {
        glUniform3f(uEmissiveFactor, 0.f, 0.f, 0.f);
      }
      if (uEmissiveTexture >= 0) {
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, 0);
        glUniform1i(uEmissiveTexture, 2);
      }  
      if (uOcclusionStrength >= 0) {
        glUniform1f(uOcclusionStrength, 0.f);
      }
      if (uOcclusionTexture >= 0) {
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, 0);
        glUniform1i(uOcclusionTexture, 3);
      }
    }
  };

  // Lambda function to draw the scene
  const auto drawScene = [&](const Camera &camera) {
    glViewport(0, 0, m_nWindowWidth, m_nWindowHeight);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const auto viewMatrix = camera.getViewMatrix();

    if(lightFromCamera){
      glUniform3f(uLightDirectionLocation, 0, 0, 1);
    }
    else{
      const auto lightDirectionViewSpace = glm::normalize(glm::vec3(viewMatrix * glm::vec4(lightDirection,0.)));
      glUniform3f(uLightDirectionLocation, lightDirectionViewSpace[0], lightDirectionViewSpace[1], lightDirectionViewSpace[2]);
    }

    if(uLightIntensity >= 0){
      glUniform3f(uLightIntensity, lightIntensity[0], lightIntensity[1], lightIntensity[2]);
    }

    if (uApplyOcclusion >= 0) {
      glUniform1i(uApplyOcclusion, applyOcclusion);
    }

    // The recursive function that should draw a node
    // We use a std::function because a simple lambda cannot be recursive
    const std::function<void(int, const glm::mat4 &)> drawNode =
        [&](int nodeIdx, const glm::mat4 &parentMatrix) {
          auto &node = model.nodes[nodeIdx];
          const glm::mat4 modelMatrix = getLocalToWorldMatrix(node, parentMatrix);

          //if node references a mesh
          if(node.mesh >= 0){
            //compute corresponding matrices, local to camera, local to screen, normal matrix
            const auto modelViewMatrix = viewMatrix * modelMatrix;
            const auto modelViewProjectionMatrix = projMatrix * modelViewMatrix;
            const auto normalMatrix = glm::transpose(glm::inverse(modelViewMatrix));

            //Get matrices to GPU
            glUniformMatrix4fv(modelViewMatrixLocation, 1, GL_FALSE, glm::value_ptr(modelMatrix));
            glUniformMatrix4fv(modelViewProjMatrixLocation, 1, GL_FALSE, glm::value_ptr(modelViewProjectionMatrix));
            glUniformMatrix4fv(normalMatrixLocation, 1, GL_FALSE, glm::value_ptr(normalMatrix));

            //Draw every single primitive of the mesh
            const auto &mesh = model.meshes[node.mesh];
            const auto &vaoRange = meshToVA[node.mesh];

            for(size_t prIdx = 0; prIdx < mesh.primitives.size(); ++prIdx){
              const auto vao = vertexArrayObjects[vaoRange.begin + prIdx];
              const auto &primitive = mesh.primitives[prIdx];
              bindMaterial(primitive.material);
              glBindVertexArray(vao);
              //for those with IBO
              if(primitive.indices >= 0){
                const auto &accessor = model.accessors[primitive.indices];
                const auto &bufferView = model.bufferViews[accessor.bufferView];
                const auto byteOffset = accessor.byteOffset + bufferView.byteOffset;
                glDrawElements(primitive.mode, GLsizei(accessor.count), accessor.componentType, (const GLvoid*)byteOffset);
              }else{ //without IBO
                const auto accessorIdx = (*begin(primitive.attributes)).second;
                const auto &accessor = model.accessors[accessorIdx];
                glDrawArrays(primitive.mode, 0, GLsizei(accessor.count));
              }
            }
            glBindVertexArray(0);
          }
          for(const auto childNodeIdx : node.children){
            drawNode(childNodeIdx, modelMatrix);
          }
        };

    // Draw the scene referenced by gltf file
    if (model.defaultScene >= 0) {
      // Draw all nodes
      const auto &nodes = model.scenes[model.defaultScene].nodes;
      for(const auto &nodeIdx : nodes){
        drawNode(nodeIdx, glm::mat4(1));
      }
    }
  };

  //Rendering image (png)
  if(!m_OutputPath.empty()){
    std::vector<unsigned char> pixels(m_nWindowWidth * m_nWindowHeight * 3);
    renderToImage(m_nWindowWidth, m_nWindowHeight, 3, pixels.data(), [&]() {
      const auto camera = cameraController->getCamera();
      drawScene(camera);
    });

    flipImageYAxis(m_nWindowWidth, m_nWindowHeight, 3, pixels.data()); //conventional since OpenGL has different image axis management

    const auto strPath = m_OutputPath.string();
    stbi_write_png(strPath.c_str(), m_nWindowWidth, m_nWindowHeight, 3, pixels.data(), 0);

    return 0;
  }

  // Loop until the user closes the window
  for (auto iterationCount = 0u; !m_GLFWHandle.shouldClose();
       ++iterationCount) {
         
    const auto seconds = glfwGetTime();

    const auto camera = cameraController->getCamera();
    drawScene(camera);

    
    // GUI code:
    imguiNewFrame();

    {
      ImGui::Begin("GUI");
      ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
          1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
      if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("eye: %.3f %.3f %.3f", camera.eye().x, camera.eye().y,
            camera.eye().z);
        ImGui::Text("center: %.3f %.3f %.3f", camera.center().x,
            camera.center().y, camera.center().z);
        ImGui::Text(
            "up: %.3f %.3f %.3f", camera.up().x, camera.up().y, camera.up().z);

        ImGui::Text("front: %.3f %.3f %.3f", camera.front().x, camera.front().y,
            camera.front().z);
        ImGui::Text("left: %.3f %.3f %.3f", camera.left().x, camera.left().y,
            camera.left().z);

        if (ImGui::Button("CLI camera args to clipboard")) {
          std::stringstream ss;
          ss << "--lookat " << camera.eye().x << "," << camera.eye().y << ","
             << camera.eye().z << "," << camera.center().x << ","
             << camera.center().y << "," << camera.center().z << ","
             << camera.up().x << "," << camera.up().y << "," << camera.up().z;
          const auto str = ss.str();
          glfwSetClipboardString(m_GLFWHandle.window(), str.c_str());
        }

        static int cameraType = 0;
        const auto trackBallRadioButton = ImGui::RadioButton("Trackball Camera", &cameraType, 0);
        ImGui::SameLine(); 
        const auto firstPersonRadioButton = ImGui::RadioButton("First Person Camera", &cameraType, 1);

        const bool cameraTypeChanged = trackBallRadioButton || firstPersonRadioButton;
        if(cameraTypeChanged){
          const auto currentCamera = cameraController->getCamera();
          if(cameraType == 0){
            cameraController = std::make_unique<TrackballCameraController>(m_GLFWHandle.window(), 0.5f * maxDist);
          }else{
            cameraController = std::make_unique<FirstPersonCameraController>(m_GLFWHandle.window(), 0.5f * maxDist);
          }
          cameraController->setCamera(currentCamera);
        }       
      }

      if(ImGui::CollapsingHeader(("Sun"), ImGuiTreeNodeFlags_DefaultOpen)){
          static float theta = 0.f;
          static float phi = 0.f;

          if (ImGui::SliderFloat("theta", &theta, 0, glm::pi<float>()) ||
            ImGui::SliderFloat("phi", &phi, 0, 2.f * glm::pi<float>())) {
              const auto sinPhi = glm::sin(phi);
              const auto cosPhi = glm::cos(phi);
              const auto sinTheta = glm::sin(theta);
              const auto cosTheta = glm::cos(theta);
              lightDirection = glm::vec3(sinTheta * cosPhi, cosTheta, sinTheta * sinPhi);
          }

          glm::vec3 lightColor(1.f, 1.f, 1.f);
          float lightIntensityFactor = 1.f;

          if(ImGui::InputFloat("intensity",&lightIntensityFactor) || ImGui::ColorEdit3("color", (float *)&lightColor)){
            lightIntensity = lightIntensityFactor * lightColor;
          }
          ImGui::Checkbox("Occlusion", &applyOcclusion);
          ImGui::Checkbox("Light from camera", &lightFromCamera);
        }         
      ImGui::End();
    }

    imguiRenderFrame();
    

    glfwPollEvents(); // Poll for and process events

    auto ellapsedTime = glfwGetTime() - seconds;
    auto guiHasFocus =
        ImGui::GetIO().WantCaptureMouse || ImGui::GetIO().WantCaptureKeyboard;
    if (!guiHasFocus) {
      cameraController->update(float(ellapsedTime));
    }

    m_GLFWHandle.swapBuffers(); // Swap front and back buffers
    
  }
  // TODO clean up allocated GL data

  return 0;
}

ViewerApplication::ViewerApplication(const fs::path &appPath, uint32_t width,
    uint32_t height, const fs::path &gltfFile,
    const std::vector<float> &lookatArgs, const std::string &vertexShader,
    const std::string &fragmentShader, const fs::path &output) :
    m_nWindowWidth(width),
    m_nWindowHeight(height),
    m_AppPath{appPath},
    m_AppName{m_AppPath.stem().string()},
    m_ImGuiIniFilename{m_AppName + ".imgui.ini"},
    m_ShadersRootPath{m_AppPath.parent_path() / "shaders"},
    m_gltfFilePath{gltfFile},
    m_OutputPath{output}
{
  if (!lookatArgs.empty()) {
    m_hasUserCamera = true;
    m_userCamera =
        Camera{glm::vec3(lookatArgs[0], lookatArgs[1], lookatArgs[2]),
            glm::vec3(lookatArgs[3], lookatArgs[4], lookatArgs[5]),
            glm::vec3(lookatArgs[6], lookatArgs[7], lookatArgs[8])};
  }

  if (!vertexShader.empty()) {
    m_vertexShader = vertexShader;
  }

  if (!fragmentShader.empty()) {
    m_fragmentShader = fragmentShader;
  }

  ImGui::GetIO().IniFilename =
      m_ImGuiIniFilename.c_str(); // At exit, ImGUI will store its windows
                                  // positions in this file

  glfwSetKeyCallback(m_GLFWHandle.window(), keyCallback);

  printGLVersion();
}

bool ViewerApplication::loadGltfFile(tinygltf::Model &model)
{
    tinygltf::TinyGLTF loader;
    std::string err;
    std::string warn;

    bool result = loader.LoadASCIIFromFile(&model, &err, &warn, m_gltfFilePath.string());

    if(!warn.empty()){
      std::cerr << "Warning : " << warn << std::endl;
    }

    if(!err.empty()){
      std::cerr << "Error : " << warn << std::endl;
    }

    if(!result){
      std::cerr << "could not complete glTF file parsing" << std::endl;
    }

    return result;
}

std::vector<GLuint> ViewerApplication::createBufferObjects(const tinygltf::Model &model)
{
  //Initialize & generate the identifiers
  std::vector<GLuint> bufferObjects(model.buffers.size(),0);
  glGenBuffers(GLsizei(bufferObjects.size()), bufferObjects.data());

  //Bind buffer data to identifiers data
  for(size_t i = 0; i < model.buffers.size(); ++i){
    glBindBuffer(GL_ARRAY_BUFFER, bufferObjects[i]);
    glBufferStorage(GL_ARRAY_BUFFER, model.buffers[i].data.size(), model.buffers[i].data.data(),0);
  }

  //Unbind array buffer
  glBindBuffer(GL_ARRAY_BUFFER,0);

  return bufferObjects;
}

std::vector<GLuint> ViewerApplication::createVertexArrayObjects(
  const tinygltf::Model &model, const std::vector<GLuint> &bufferObjects,
  std::vector<VaoRange> &meshToVA)
{
  std::vector<GLuint> vertexArrayObjects;
  //For each range of model, keep its range of vao
  meshToVA.resize(model.meshes.size());

  //Loop on all meshes
  for(size_t i = 0; i < model.meshes.size(); ++i)
  {
    tinygltf::Mesh mesh = model.meshes[i];
    VaoRange &vaoRange = meshToVA[i];

    vaoRange.begin = static_cast<GLsizei>(vertexArrayObjects.size());
    vaoRange.count = static_cast<GLsizei>(mesh.primitives.size());

    vertexArrayObjects.resize(vertexArrayObjects.size() + model.meshes[i].primitives.size());
    glGenVertexArrays(vaoRange.count, &vertexArrayObjects[vaoRange.begin]);

    //Loop on the primitives of the current mesh
    for(size_t pIdx = 0; pIdx < mesh.primitives.size(); ++pIdx)
    {
      
      const auto &primitive = mesh.primitives[pIdx];
      const auto vao = vertexArrayObjects[vaoRange.begin + pIdx];
            
      glBindVertexArray(vao);

      { //POSITION
        const auto iterator = primitive.attributes.find("POSITION");
        if(iterator != end(primitive.attributes)){
          const auto accessorIdx = (*iterator).second;
          const auto &accessor = model.accessors[accessorIdx];
          const auto &bufferView = model.bufferViews[accessor.bufferView];
          const auto bufferIdx = bufferView.buffer;

          glEnableVertexAttribArray(VERTEX_ATTRIB_POSITION_IDX);
          glBindBuffer(GL_ARRAY_BUFFER, bufferObjects[bufferIdx]);

          const auto byteOffset = accessor.byteOffset + bufferView.byteOffset;
          glVertexAttribPointer(VERTEX_ATTRIB_POSITION_IDX, accessor.type,
            accessor.componentType, GL_FALSE, GLsizei(bufferView.byteStride),
            (const GLvoid *)byteOffset);
        }
      }

      { //NORMAL
        const auto iterator = primitive.attributes.find("NORMAL");
        if(iterator != end(primitive.attributes)){
          const auto accessorIdx = (*iterator).second;
          const auto &accessor = model.accessors[accessorIdx];
          const auto &bufferView = model.bufferViews[accessor.bufferView];
          const auto bufferIdx = bufferView.buffer;

          glEnableVertexAttribArray(VERTEX_ATTRIB_NORMAL_IDX);
          glBindBuffer(GL_ARRAY_BUFFER, bufferObjects[bufferIdx]);

          const auto byteOffset = accessor.byteOffset + bufferView.byteOffset;
          glVertexAttribPointer(VERTEX_ATTRIB_NORMAL_IDX, accessor.type,
            accessor.componentType, GL_FALSE, GLsizei(bufferView.byteStride),
            (const GLvoid *)(accessor.byteOffset + bufferView.byteOffset));
        }
      }

      {const auto iterator = primitive.attributes.find("TEXCOORD_0");
        if (iterator != end(primitive.attributes)) {
          const auto accessorIdx = (*iterator).second;
          const auto &accessor = model.accessors[accessorIdx];
          const auto &bufferView = model.bufferViews[accessor.bufferView];
          const auto bufferIdx = bufferView.buffer;
          const auto bufferObject = model.buffers[bufferIdx];

          glEnableVertexAttribArray(VERTEX_ATTRIB_TEXCOORD0_IDX);

          glBindBuffer(GL_ARRAY_BUFFER,bufferObjects[bufferIdx]);
          assert(GL_ARRAY_BUFFER == bufferView.target);

          const auto byteOffset = accessor.byteOffset + bufferView.byteOffset;
          glVertexAttribPointer(VERTEX_ATTRIB_TEXCOORD0_IDX, accessor.type,
              accessor.componentType, GL_FALSE, GLsizei(bufferView.byteStride),
              (const GLvoid *)byteOffset);
        }
      }
      if (model.meshes[i].primitives[pIdx].indices >= 0) {
        const auto accessorIdx = model.meshes[i].primitives[pIdx].indices;
        const auto &accessor = model.accessors[accessorIdx];
        const auto &bufferView = model.bufferViews[accessor.bufferView];
        const auto bufferIdx = bufferView.buffer;

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bufferObjects[bufferIdx]);
      }
    }
  }
  return vertexArrayObjects;
}

std::vector<GLuint> ViewerApplication::createTextureObjects(const tinygltf::Model &model) const {
  //Texture identifiers
  std::vector<GLuint> textureObjects(model.textures.size(), 0);
  glGenTextures(GLsizei(textureObjects.size()), &textureObjects[0]);

  //Default sampler
  tinygltf::Sampler defaultSampler;
  defaultSampler.minFilter = GL_LINEAR;
  defaultSampler.magFilter = GL_LINEAR;
  defaultSampler.wrapS = GL_REPEAT;
  defaultSampler.wrapT = GL_REPEAT;
  defaultSampler.wrapR = GL_REPEAT;

  //Loop each texture
  for(size_t i = 0; i < model.textures.size(); ++i){
    const auto &texture = model.textures[i]; //get texture
    assert(texture.source >= 0);
    const auto &image = model.images[texture.source]; 
    //get matching sampler or default sampler
    const auto &sampler = texture.sampler >= 0 ? model.samplers[texture.sampler] : defaultSampler;

    //Bind texture object to target
    glBindTexture(GL_TEXTURE_2D, textureObjects[i]);
    //Image data Setting
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width, image.height, 0, GL_RGBA, image.pixel_type, image.image.data());

    //Sampling parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, sampler.minFilter != -1 ? sampler.minFilter : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, sampler.magFilter != -1 ? sampler.magFilter : GL_LINEAR);
    //Wrapping parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, sampler.wrapS);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, sampler.wrapT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, sampler.wrapR);

    if (sampler.minFilter == GL_NEAREST_MIPMAP_NEAREST ||
        sampler.minFilter == GL_NEAREST_MIPMAP_LINEAR ||
        sampler.minFilter == GL_LINEAR_MIPMAP_NEAREST ||
        sampler.minFilter == GL_LINEAR_MIPMAP_LINEAR) {
      glGenerateMipmap(GL_TEXTURE_2D);
    }
  }
  glBindTexture(GL_TEXTURE_2D, 0);
  return textureObjects;
}


