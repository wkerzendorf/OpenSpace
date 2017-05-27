/*****************************************************************************************
*                                                                                       *
* OpenSpace                                                                             *
*                                                                                       *
* Copyright (c) 2014-2017                                                               *
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

#include <modules/globebrowsing/models/renderableroversurface.h>
#include <ghoul/logging/logmanager.h>
#include <openspace/scene/scenegraphnode.h>
#include <ghoul/filesystem/filesystem.h>
#include <openspace/engine/openspaceengine.h>
#include <modules/globebrowsing/chunk/chunknode.h>
#include <modules/globebrowsing/models/modelprovider.h>

#include <ghoul/io/texture/texturereader.h>

#include <fstream>
#include <gdal_priv.h>
#include "ogrsf_frmts.h"
#include <glm/gtx/quaternion.hpp>

namespace {
	const std::string _loggerCat		= "RenderableRoverSurface";
	const char* keyRoverLocationPath	= "RoverLocationPath";
	const char* keyModelPath			= "ModelPath";
	const char* keyTexturePath			= "TexturePath";
	const char* keyName					= "Name";
	const char* keyAbsPathToTextures	= "AbsPathToTextures";
	const char* keyAbsPathToModels		= "AbsPathToModels";
}

namespace openspace {

using namespace properties;

namespace globebrowsing {
	RenderableRoverSurface::RenderableRoverSurface(const ghoul::Dictionary & dictionary)
		: Renderable(dictionary),
		_generalProperties({
			BoolProperty("enabled", "enabled", false)
	})
		, _debugModelRotation("modelrotation", "Model Rotation", glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(360.0f))
		, _modelSwitch()
		, _prevLevel(3)
		, _isFirst(true)
{
	if (!dictionary.getValue(keyRoverLocationPath, _roverLocationPath))
		throw ghoul::RuntimeError(std::string(keyRoverLocationPath) + " must be specified!");

	if (!dictionary.getValue(keyModelPath, _modelPath))
		throw ghoul::RuntimeError(std::string(keyModelPath) + " must be specified!");

	if (!dictionary.getValue(keyTexturePath, _texturePath))
		throw ghoul::RuntimeError(std::string(keyTexturePath) + " must be specified!");

	std::string name;
	bool success = dictionary.getValue(SceneGraphNode::KeyName, name);
	ghoul_assert(success, "Name was not passed to RenderableSite");

	// Save abs path to the models and texture folders because the abs path is changed when leaving constructor.
	_absModelPath = absPath(_modelPath);
	_absTexturePath = absPath(_texturePath);

	// Extract all subsites that has models
	ghoul::Dictionary tempDictionary;
	tempDictionary.setValue(keyRoverLocationPath, _roverLocationPath);
	tempDictionary.setValue(keyAbsPathToTextures, _absTexturePath);
	tempDictionary.setValue(keyAbsPathToModels, _absModelPath);
	_subSitesWithModels = RoverPathFileReader::extractSubsitesWithModels(tempDictionary);

	// Extract all subsites
	ghoul::Dictionary tempDictionary2;
	tempDictionary2.setValue(keyRoverLocationPath, _roverLocationPath);
	_subSites = RoverPathFileReader::extractAllSubsites(tempDictionary2);

	addProperty(_debugModelRotation);

	_cachingModelProvider = std::make_shared<CachingSurfaceModelProvider>(this);
	_renderableExplorationPath = std::make_shared<RenderableExplorationPath>();
}

bool RenderableRoverSurface::initialize() {
	std::vector<Geodetic2> coordinates;
	for (auto subsite : _subSites) {
		coordinates.push_back(subsite->geodetic);
	}

	std::string ownerName = owner()->name();
	auto parent = OsEng.renderEngine().scene()->sceneGraphNode(ownerName)->parent();

	_globe = (globebrowsing::RenderableGlobe*)parent->renderable();

	_renderableExplorationPath->initialize(_globe, coordinates);

	_chunkedLodGlobe = _globe->chunkedLodGlobe();

	_modelSwitch.initialize(_globe);

	_chunkedLodGlobe->addSites(_subSitesWithModels);

	RenderEngine& renderEngine = OsEng.renderEngine();
	_programObject = renderEngine.buildRenderProgram("RenderableRoverSurface",
		"${MODULE_GLOBEBROWSING}/shaders/fullsubsite_vs.glsl",
		"${MODULE_GLOBEBROWSING}/shaders/fullsubsite_fs.glsl");

	return true;
}

bool RenderableRoverSurface::deinitialize() {
	return false;
}

bool RenderableRoverSurface::isReady() const {
	return true;
}

void RenderableRoverSurface::render(const RenderData& data) {
	_models.clear();

	std::vector<std::vector<std::shared_ptr<Subsite>>> subSitesVector = _chunkedLodGlobe->subsites();

	if (subSitesVector.size() < 1) {
		return;
	}

	std::vector<std::shared_ptr<Subsite>> ss;
	ghoul::Dictionary modelDic;
	std::unique_ptr<ModelProvider>_modelProvider;
	int level;
	switch (_modelSwitch.getLevel(data)) {
		case LodModelSwitch::Mode::Low :	
			//Low
			modelDic.setValue("Type", "MultiModelProvider");
			_modelProvider = std::move(ModelProvider::createFromDictionary(modelDic));
			ss = _modelProvider->calculate(subSitesVector, data);
			level = 2;
		
			break;
		case LodModelSwitch::Mode::Close :
			//Close
			if (_isFirst) LERROR("GOING CLOSE");
			_isFirst = false;
			modelDic.setValue("Type", "SingleModelProvider");
			_modelProvider = std::move(ModelProvider::createFromDictionary(modelDic));
			ss = _modelProvider->calculate(subSitesVector, data);
			level = 3;
			break;

		case LodModelSwitch::Mode::High :
			//High up
			level = 1;
			break;

		case LodModelSwitch::Mode::Far:
			//Far away
			// Only used to decide if renderableexplorationpath should be visible or not atm.
			level = 0;
			break;
	}

	_renderableExplorationPath->setLevel(level);
	_renderableExplorationPath->render(data);
	_cachingModelProvider->setLevel(level);

	//TODO: MAKE CACHE AWARE OF PREVIOUS LEVEL
	//FOR ALPHA BLENDING TO WORK
	std::vector<std::shared_ptr<SubsiteModels>> _subsiteModels = _cachingModelProvider->getModels(ss, level);
	
	if (_subsiteModels.size() == 0) return;

	if (level == 3 && _prevSubsite != nullptr 
		&& _prevSubsite->drive != _subsiteModels.at(0)->drive) {
		_subsiteModels.push_back(_prevSubsite);
	}

	_subsiteModels = calculateSurfacePosition(_subsiteModels);

	_programObject->activate();
	for (auto subsiteModels : _subsiteModels) {

		float dir = 1;
		float alpha = subsiteModels->_alpha;
		int subsitePrevLevel = subsiteModels->level;
		if (level != _prevLevel) {
			// We went from one level to the next
			// we need to fade out previously rendered models
			if (level != subsitePrevLevel && alpha >= 0.0) {
				// Current subsite should fade out
				dir *= -1;
			}
			else if (alpha >= 1.0) {
				// Current subsite is the correct level and has been faded in correctly
				dir = 0;
			}

			if (level == 3) {
				_prevSubsite = subsiteModels;
			}
		}
		else if (_prevSubsite != nullptr 
			&& _prevSubsite->drive != subsiteModels->drive 
			&& level == 3) {
			// Walking between models
			// And this is the new model that should be faded in
			// if it's not already done
			dir = subsiteModels->_alpha >= 1.0 ? 0 : 1;
			if (dir == 0.0) {
				_prevSubsite = subsiteModels;
			}
		}
		else if (_prevSubsite != nullptr 
			&& _subsiteModels.size() > 1
			&& _prevSubsite->drive == subsiteModels->drive 
			&& level == 3) {
			// Walking between models
			// And this is the previous model that should be faded out
			// if it's not already done
			dir = subsiteModels->_alpha <= 0.0 ? 0 : -1;
		}
		else {
			dir = subsiteModels->_alpha >= 1.0 ? 0 : 1;
		}

		alpha = alpha + dir * 0.005;
		subsiteModels->_alpha = alpha;

		glm::dmat4 globeTransform = _globe->modelTransform();

		glm::dvec3 positionWorldSpace = globeTransform * glm::dvec4(subsiteModels->cartesianPosition, 1.0);
		glm::dvec3 positionWorldSpace2 = glm::dvec4(subsiteModels->cartesianPosition, 1.0);

		// debug rotation controlled from GUI
		glm::mat4 unitMat4(1);
		glm::vec3 debugEulerRot = glm::radians(_debugModelRotation.value());

		//debugEulerRot.x = glm::radians(146.f);
		//debugEulerRot.y = glm::radians(341.f);
		//debugEulerRot.z = glm::radians(79.f);

		glm::mat4 rotX = glm::rotate(unitMat4, debugEulerRot.x, glm::vec3(1, 0, 0));
		glm::mat4 rotY = glm::rotate(unitMat4, debugEulerRot.y, glm::vec3(0, 1, 0));
		glm::mat4 rotZ = glm::rotate(unitMat4, debugEulerRot.z, glm::vec3(0, 0, 1));

		glm::dmat4 debugModelRotation = rotX * rotY * rotZ;

		// Rotation to make model up become normal of position on ellipsoid
		glm::dvec3 surfaceNormal = _globe->ellipsoid().geodeticSurfaceNormal(subsiteModels->siteGeodetic);

		surfaceNormal = glm::normalize(surfaceNormal);
		float cosTheta = dot(glm::dvec3(0, 0, 1), surfaceNormal);
		glm::dvec3 rotationAxis;

		rotationAxis = cross(glm::dvec3(0, 0, 1), surfaceNormal);

		float s = sqrt((1 + cosTheta) * 2);
		float invs = 1 / s;

		glm::dquat rotationMatrix = glm::dquat(s * 0.5f, rotationAxis.x * invs, rotationAxis.y * invs, rotationAxis.z * invs);

		glm::dvec3 xAxis = _globe->ellipsoid().geodeticSurfaceNorthPoleTangent(positionWorldSpace2);

		if (xAxis.x == 0 && xAxis.y == 0 && xAxis.z == 0) {
			LERROR("PLANE AND LINE HAS SAME");
		}

		glm::dvec4 test = glm::rotate(rotationMatrix, glm::dvec4(0, -1, 0, 1));

		glm::dvec3 testa = glm::dvec3(test.x, test.y, test.z);

		float cosTheta2 = dot(testa, xAxis);
		glm::dvec3 rotationAxis2;

		rotationAxis2 = cross(testa, xAxis);

		float s2 = sqrt((1 + cosTheta2) * 2);
		float invs2 = 1 / s2;

		glm::quat rotationMatrix2 = glm::quat(s2 * 0.5f, rotationAxis2.x * invs2, rotationAxis2.y * invs2, rotationAxis2.z * invs2);

		glm::dmat4 modelTransform =
			glm::translate(glm::dmat4(1.0), positionWorldSpace) *
			glm::dmat4(data.modelTransform.rotation) *
			glm::dmat4(glm::toMat4(rotationMatrix2)) *
			glm::dmat4(glm::toMat4(rotationMatrix)) *
			debugModelRotation;

		glm::dmat4 modelViewTransform = data.camera.combinedViewMatrix() * modelTransform;
		glm::vec3 directionToSun = glm::normalize(_sunPos - positionWorldSpace);
		glm::vec3 directionToSunViewSpace = glm::mat3(data.camera.combinedViewMatrix()) * directionToSun;

		std::vector<glm::fvec3> cameraInfoCenter;
		std::vector<glm::fvec3> cameraInfoAxis;
		std::vector<glm::fvec3> cameraInfoHorizontal;
		std::vector<glm::fvec3> cameraInfoVector;

		for (auto cameraInfo : subsiteModels->cameraInfoVector) {
			ImgReader::PointCloudInfo mInfoTemp = cameraInfo;
			cameraInfoCenter.push_back(mInfoTemp._cameraCenter);
			cameraInfoAxis.push_back(mInfoTemp._cameraAxis);
			cameraInfoHorizontal.push_back(mInfoTemp._cameraHorizontal);
			cameraInfoVector.push_back(mInfoTemp._cameraVector);
		}

		//(*it)._programObject->setUniform("camerasCenters", cameraInfoCenter);
		//(*it)._programObject->setUniform("camerasAxes", cameraInfoAxis);
		//(*it)._programObject->setUniform("camerasHorizontals", cameraInfoHorizontal);
		//(*it)._programObject->setUniform("camerasVectors", cameraInfoVector);

		const GLint locationCenter = _programObject->uniformLocation("camerasCenters");
		const GLint locationAxis = _programObject->uniformLocation("camerasAxes");
		const GLint locationHorizontal = _programObject->uniformLocation("camerasHorizontals");
		const GLint locationVector = _programObject->uniformLocation("camerasVectors");

		glUniform3fv(locationCenter, cameraInfoCenter.size(), reinterpret_cast<GLfloat *>(cameraInfoCenter.data()));
		glUniform3fv(locationAxis, cameraInfoAxis.size(), reinterpret_cast<GLfloat *>(cameraInfoAxis.data()));
		glUniform3fv(locationHorizontal, cameraInfoHorizontal.size(), reinterpret_cast<GLfloat *>(cameraInfoHorizontal.data()));
		glUniform3fv(locationVector, cameraInfoVector.size(), reinterpret_cast<GLfloat *>(cameraInfoVector.data()));

		//TODO: Is this really used? Otherwise delete this and _sunPos.
		_programObject->setUniform("modelViewTransform", glm::mat4(modelViewTransform));
		_programObject->setUniform("projectionTransform", data.camera.projectionMatrix());
		//_programObject->setUniform("fading", alpha);

		_programObject->setUniform("size", static_cast<int>(cameraInfoCenter.size()));

		glEnable(GL_BLEND);
		glDisable(GL_CULL_FACE);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		//model->geometry->setUniforms(*_programObject);
		glBindTexture(GL_TEXTURE_2D_ARRAY, subsiteModels->textureID);
		subsiteModels->model->render();
	}
	_programObject->deactivate();
	_prevLevel = level;
}

void RenderableRoverSurface::update(const UpdateData& data) {
	_renderableExplorationPath->update(data);
	_cachingModelProvider->update(this);
	// Faster to store a reference to the node and call worldPosition() from that node?
	// Might not have to traverse the scenegraph like that.
	_sunPos = OsEng.renderEngine().scene()->sceneGraphNode("Sun")->worldPosition();
}

std::vector<std::shared_ptr<SubsiteModels>> RenderableRoverSurface::calculateSurfacePosition(std::vector<std::shared_ptr<SubsiteModels>> vector) {
	for (auto subsiteModels : vector) {
		glm::dvec3 positionModelSpaceTemp = _globe->ellipsoid().cartesianSurfacePosition(subsiteModels->geodetic);
		double heightToSurface = _globe->getHeight(positionModelSpaceTemp);

		globebrowsing::Geodetic3 geo3 = globebrowsing::Geodetic3{ subsiteModels->geodetic , heightToSurface + 2.0 };
		subsiteModels->cartesianPosition = _globe->ellipsoid().cartesianPosition(geo3);
	}
	return vector;
}

} // namespace globebrowsing
} // namepsace openspace

