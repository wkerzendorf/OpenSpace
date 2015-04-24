/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2015                                                               *
 *                                                                                       *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this  *
 * software and associated documentation files (the "Software"), to deal in the Software *
 * without restriction, including without limitation the rights to use, copy, modify,    *
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to    *
 * permit persons to whom the Software is furnished to do so, subject to the following   *
 * conditions:                                                                           *
 *                                                                                       *
 * The above copyright notice and this permission notice shall be included in all copies *
 * or substantial portions of the Software.                                              *
 *                                                                                       *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,   *
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A         *
 * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT    *
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF  *
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE  *
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                                         *
 ****************************************************************************************/

// temporary includes (will fix as soon as I figure out how class hierarchy should work, 
//                     ie after I see model on screen)

// open space includes
#include <openspace/rendering/model/renderablemodel.h>
#include <openspace/util/constants.h>
#include <openspace/rendering/model/modelgeometry.h>
#include <openspace/engine/configurationmanager.h>

#include <ghoul/io/texture/texturereader.h>
#include <ghoul/opengl/textureunit.h>
#include <ghoul/filesystem/filesystem.h>

#include <openspace/util/time.h>
#include <openspace/util/spicemanager.h>

#include <openspace/engine/openspaceengine.h>
#include <sgct.h>
#include "imgui.h"

namespace {
	const std::string _loggerCat = "RenderableModel";
	const std::string keySource      = "Rotation.Source";
	const std::string keyDestination = "Rotation.Destination";
	const std::string keyBody = "Body";
	const std::string keyStart = "StartTime";
	const std::string keyEnd = "EndTime";
	const std::string keyFading = "Shading.Fadeable";

}

namespace openspace {

RenderableModel::RenderableModel(const ghoul::Dictionary& dictionary)
    : Renderable(dictionary)
	, _colorTexturePath("colorTexture", "Color Texture")
    , _programObject(nullptr)
    , _texture(nullptr)
	, _geometry(nullptr)
	, _performShading("performShading", "Perform Shading", true)
	, _alpha(1.f)
	, _fading("fading", "Fade", 0)
	, _performFade("performFading", "Perform Fading", false)
{
	std::string name;
    bool success = dictionary.getValue(constants::scenegraphnode::keyName, name);
    ghoul_assert(success, "Name was not passed to RenderableModel");
	std::string path;
	success = dictionary.getValue(constants::scenegraph::keyPathModule, path);
    ghoul_assert(success, "Module path was not passed to RenderableModel");

	ghoul::Dictionary geometryDictionary;
	success = dictionary.getValue(
		constants::renderablemodel::keyGeometry, geometryDictionary);
	if (success) {
		geometryDictionary.setValue(constants::scenegraphnode::keyName, name);
		geometryDictionary.setValue(constants::scenegraph::keyPathModule, path);
		_geometry = modelgeometry::ModelGeometry::createFromDictionary(geometryDictionary);
	}

	std::string texturePath = "";
	success = dictionary.getValue("Textures.Color", texturePath);
	if (success)
		_colorTexturePath = path + "/" + texturePath;

	addPropertySubOwner(_geometry);

	addProperty(_colorTexturePath);
	_colorTexturePath.onChange(std::bind(&RenderableModel::loadTexture, this));

	dictionary.getValue(keySource, _source);
	dictionary.getValue(keyDestination, _destination);
	dictionary.getValue(keyBody, _target);

    setBoundingSphere(pss(1.f, 9.f));
	addProperty(_performShading);

	if (dictionary.hasKeyAndValue<bool>(keyFading)) {
		bool fading;
		dictionary.getValue(keyFading, fading);
		_performFade = fading;
	}
	addProperty(_performFade);
}

bool RenderableModel::isReady() const {
	bool ready = true;
	ready &= (_programObject != nullptr);
	ready &= (_texture != nullptr);
	return ready;
}

bool RenderableModel::initialize() {
    bool completeSuccess = true;
    if (_programObject == nullptr)
        completeSuccess
              &= OsEng.ref().configurationManager()->getValue("GenericModelShader", _programObject); 

    loadTexture();

    completeSuccess &= (_texture != nullptr);
    completeSuccess &= _geometry->initialize(this); 
	completeSuccess &= !_source.empty();
	completeSuccess &= !_destination.empty();

    return completeSuccess;
}

bool RenderableModel::deinitialize() {
	if (_geometry) {
		_geometry->deinitialize();
		delete _geometry;
	}
	if (_texture)
		delete _texture;

	_geometry = nullptr;
	_texture = nullptr;
	return true;
}

void RenderableModel::render(const RenderData& data) {
    _programObject->activate();

	double lt;
    glm::mat4 transform = glm::mat4(1);

	glm::mat4 tmp = glm::mat4(1);
	for (int i = 0; i < 3; i++){
		for (int j = 0; j < 3; j++){
			tmp[i][j] = static_cast<float>(_stateMatrix[i][j]);
		}
	}
	transform *= tmp;
	
	double time = openspace::Time::ref().currentTime();
	bool targetPositionCoverage = openspace::SpiceManager::ref().hasSpkCoverage(_target, time);
	if (!targetPositionCoverage){
		int frame = ImGui::GetFrameCount() % 180;
		float fadingFactor = sin((frame * pi_c()) / 180);
		_alpha = 0.5f + fadingFactor*0.5f;
		//_texture = "";
	}
	else
		_alpha = 1.0f;

	psc tmppos;
	SpiceManager::ref().getTargetPosition(_source, "SUN", "GALACTIC", "NONE", time, tmppos, lt);
	glm::vec3 cam_dir = glm::normalize(data.camera.position().vec3() - tmppos.vec3());
	_programObject->setUniform("cam_dir", cam_dir);
	_programObject->setUniform("transparency", _alpha);
	_programObject->setUniform("sun_pos", _sunPosition.vec3());
	_programObject->setUniform("ViewProjection", data.camera.viewProjectionMatrix());
	_programObject->setUniform("ModelTransform", transform);
	setPscUniforms(_programObject, &data.camera, data.position);
	
	_programObject->setUniform("_performShading", _performShading);

	if (_performFade && _fading > 0.f){
		_fading = _fading - 0.01f;
	}
	else if (!_performFade && _fading < 1.f){
		_fading = _fading + 0.01f;

	}

	_programObject->setUniform("fading", _fading);

    // Bind texture
    ghoul::opengl::TextureUnit unit;
    unit.activate();
    _texture->bind();
    _programObject->setUniform("texture1", unit);

	_geometry->render();

    // disable shader
    _programObject->deactivate();
}

void RenderableModel::update(const UpdateData& data) {
	// set spice-orientation in accordance to timestamp
    if (!_source.empty())
	    openspace::SpiceManager::ref().getPositionTransformMatrix(_source, _destination, data.time, _stateMatrix);

    double  lt;
	openspace::SpiceManager::ref().getTargetPosition("SUN", _target, "GALACTIC", "NONE", data.time, _sunPosition, lt);
}

void RenderableModel::loadTexture() {
    delete _texture;
    _texture = nullptr;
    if (_colorTexturePath.value() != "") {
        _texture = ghoul::io::TextureReader::ref().loadTexture(absPath(_colorTexturePath));
        if (_texture) {
            LDEBUG("Loaded texture from '" << absPath(_colorTexturePath) << "'");
            _texture->uploadTexture();
        }
    }
}

}  // namespace openspace
