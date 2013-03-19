/*
 * MayaEncoder.cpp
 *
 *  Created on: Sep 11, 2012
 *      Author: shaegler
 */

#include <iostream>
#include <sstream>
#include <vector>
#include <numeric>

#include "boost/filesystem/path.hpp"

#include "util/StringUtils.h"
#include "util/Timer.h"
#include "util/Exception.h"

#include "prt/prt.h"

#include "prtx/Exception.h"
#include "prtx/Log.h"
#include "prtx/Geometry.h"
#include "prtx/Material.h"
#include "prtx/IShape.h"
#include "prtx/ShapeIterator.h"
#include "prtx/EncodePreparator.h"
#include "prtx/ExtensionManager.h"

#include "encoder/MayaEncoder.h"
#include "prtx/IGenerateContext.h"


const std::wstring MayaEncoder::ID          = L"com.esri.prt.codecs.maya.MayaEncoder";
const std::wstring MayaEncoder::NAME        = L"Autodesk(tm) Maya(tm) Encoder";
const std::wstring MayaEncoder::DESCRIPTION	= L"Encodes geometry into Autodesk Maya format.";


MayaEncoder::MayaEncoder() {
}


MayaEncoder::~MayaEncoder() {
}


void MayaEncoder::encode(prtx::IGenerateContext& context, size_t initialShapeIndex) {
	prtx::AbstractResolveMapPtr am = context.getResolveMap();

	IMayaOutputHandler* oh = dynamic_cast<IMayaOutputHandler*>(getCallbacks());
	if(oh == 0) throw(prtx::StatusException(prt::STATUS_ILLEGAL_CALLBACK_OBJECT));

	util::Timer tim;

	prtx::EncodePreparatorPtr encPrep = prtx::EncodePreparator::create();
	prtx::LeafIteratorPtr li = prtx::LeafIterator::create(context, initialShapeIndex);
	for (prtx::IShapePtr shape = li->getNext(); shape != 0; shape = li->getNext()) {
		encPrep->add(/*initialShapes[i],*/ shape);
		//			log_trace(L"encode leaf shape mat: %ls", shape->getMaterial()->getString(L"name"));
	}

	const float t1 = tim.stop();
	tim.start();

	prtx::GeometryPtrVector geometries;
	prtx::MaterialPtrVector mat;
	encPrep->createEncodableGeometriesAndMaterialsAndReset(geometries, mat);
	const prtx::InitialShape& ishape = context.getInitialShape(initialShapeIndex);
	size_t   start = 0;
	size_t   end   = 0;
	wchar_t* ruleFile = wcsdup(ishape.getRuleFile());
	for(size_t i = 0; ruleFile[i]; i++) {
		switch(ruleFile[i]) {
		case '\\':
		case '/':
			start = i + 1;
			break;
		case '.':
			ruleFile[i] = '_';
			end = i;
			break;
		}
	}
	ruleFile[end] = 0;
	std::wstring cgbName = std::wstring(&ruleFile[start]);
	free(ruleFile);
	convertGeometry(cgbName, am, geometries, mat, oh);

	const float t2 = tim.stop();
	log_info("MayaEncoder::encode() : preparator %f s, encoding %f s, total %f s") % t1 % t2 % (t1+t2);

	log_trace("MayaEncoder::encode done.");
}

#define USE_NORMALS

void MayaEncoder::convertGeometry(const std::wstring& cgbName, const prtx::AbstractResolveMapPtr am, const prtx::GeometryPtrVector& geometries, const prtx::MaterialPtrVector& mats, IMayaOutputHandler* mayaOutput) {
	log_trace("MayaEncoder::convertGeometry: begin");
	std::vector<double> vertices;
	std::vector<int>    counts;
	std::vector<int>    connects;
	int base    = 0;

#ifdef USE_NORMALS
	std::vector<double> normals;
	std::vector<int>    normalCounts;
	std::vector<int>    normalConnects;
	int nrmBase = 0;
#endif

	std::vector<float>  tcsU, tcsV;
	std::vector<int>    uvCounts;
	std::vector<int>    uvConnects;
	int uvBase  = 0;

	for(size_t gi = 0, geoCount = geometries.size(); gi < geoCount; ++gi) {
		prtx::Geometry* geo = geometries[gi].get();

		const prtx::MeshPtrVector& meshes = geo->getMeshes();
		for(size_t mi = 0, meshCount = meshes.size(); mi < meshCount; mi++) {
			prtx::Mesh* mesh = meshes[mi].get();

			const prtx::FacePtrVector& faces    = mesh->getFaces();
			const prtx::DoubleVector&  verts    = mesh->getVertexCoords();
#ifdef USE_NORMALS
			const prtx::DoubleVector&  norms    = mesh->getVertexNormalsCoords();
#endif
			bool                       hasUVS   = mesh->getUVCoords().size() > 0;
			size_t                     uvsCount = 0;

			vertices.reserve(    vertices.size()     + verts.size());
#ifdef USE_NORMALS
			normals.reserve(     normals.size()      + norms.size());
			normalCounts.reserve(normalCounts.size() + faces.size());
#endif
			counts.reserve(      counts.size()       + faces.size());
			uvCounts.reserve(    uvCounts.size()     + faces.size());

			for(size_t i = 0, size = verts.size(); i < size; ++i)
				vertices.push_back(verts[i]);

#ifdef USE_NORMALS
			for(size_t i = 0, size = norms.size(); i < size; ++i)
				normals.push_back(norms[i]);
#endif

			if(hasUVS) {
				const prtx::DoubleVector& uvs = mesh->getUVCoords(0);
				uvsCount                      = uvs.size();

				tcsU.reserve(tcsU.size() + uvsCount / 2);
				tcsV.reserve(tcsV.size() + uvsCount / 2);

				for(size_t i = 0, size = uvsCount; i < size; i += 2) {
					tcsU.push_back((float)uvs[i+0]);
					tcsV.push_back((float)uvs[i+1]);
				}
			}

			for(size_t fi = 0, faceCount = faces.size(); fi < faceCount; ++fi) {
				const prtx::FacePtr face = faces[fi];

				/*
				log_trace("    -- face %d") % fi;
				log_trace("       vtx index count: %d") % face->getVertexIndices().size();
				log_trace("       nrm index count: %d") % face->getVertexNormalsIndices().size();
				*/

				const prtx::IndexVector&	vidxs = face->getVertexIndices();
				counts.push_back((int)vidxs.size());
				for(size_t vi = 0, size = vidxs.size(); vi < size; ++vi)
					connects.push_back(base + vidxs[vi]);

#ifdef USE_NORMALS
				const prtx::IndexVector&	nidxs = face->getVertexNormalsIndices();
				normalCounts.push_back((int)nidxs.size());
				for(size_t ni = 0, size = nidxs.size(); ni < size; ++ni)
					normalConnects.push_back(nrmBase + nidxs[ni]);
#endif

				if(hasUVS) {
					const prtx::IndexVector& uvidxs = face->getUVIndices(0);
					uvCounts.push_back((int)uvidxs.size());
					for(size_t vi = 0, size = uvidxs.size(); vi < size; ++vi)
						uvConnects.push_back(uvBase + uvidxs[vi]);
				} else
					uvCounts.push_back(0);
			}

			base	  += (int)verts.size() / 3;
#ifdef USE_NORMALS
			nrmBase	+= (int)norms.size() / 3;
#endif
			uvBase  += (int)uvsCount     / 2;
		}
	}

	bool hasUVS     = tcsU.size() > 0;
#ifdef USE_NORMALS
	bool hasNormals = normals.size() > 0;
#endif

	mayaOutput->setVertices(&vertices[0], vertices.size());
	mayaOutput->setUVs(hasUVS ? &tcsU[0] : 0, hasUVS ? &tcsV[0] : 0, tcsU.size());
#ifdef USE_NORMALS
	mayaOutput->setNormals(hasNormals ? &normals[0] : 0, normals.size());
	mayaOutput->setFaces(&counts[0], counts.size(), &connects[0], connects.size(), hasNormals ? &normalCounts[0] : 0, normalCounts.size(), hasNormals ? &normalConnects[0] : 0, normalConnects.size(), hasUVS ? &uvCounts[0] : 0, uvCounts.size(), hasUVS ? &uvConnects[0] : 0, uvConnects.size());
#else
	mayaOutput->setFaces(&counts[0], counts.size(), &connects[0], connects.size(), 0, 0, 0, 0, hasUVS ? &uvCounts[0] : 0, uvCounts.size(), hasUVS ? &uvConnects[0] : 0, uvConnects.size());
#endif
	mayaOutput->createMesh();

	int startFace = 0;
	for(size_t gi = 0, geoCount = geometries.size(); gi < geoCount; ++gi) {
		prtx::Geometry* geo = geometries[gi].get();

		prtx::MaterialPtr mat = mats[gi];

		std::wostringstream matName;
		matName << "m" << cgbName << gi;

		log_wtrace(L"creating material: '%s'") % matName.str();

		int faceCount = 0;
		const prtx::MeshPtrVector& meshes = geo->getMeshes();
		for(size_t mi = 0, meshCount = meshes.size(); mi < meshCount; mi++)
			faceCount += (int)meshes[mi]->getFaces().size();

		int mh = mayaOutput->matCreate(matName.str().c_str(), startFace, faceCount);

		std::wstring tex;
		if(mat->diffuseMap().size() > 0 && mat->diffuseMap()[0]->isValid()) {
			prtx::URI texURI = mat->diffuseMap()[0]->getURI();
			log_wtrace(L"trying to set texture uri: %s") % texURI;
			mayaOutput->matSetDiffuseTexture(mh, texURI.getPath().c_str());
		}

		startFace += faceCount;
	}

	mayaOutput->finishMesh();
}

void MayaEncoder::init(prtx::IGenerateContext& /*context*/) {
}

void MayaEncoder::finish(prtx::IGenerateContext& /*context*/) {
}


