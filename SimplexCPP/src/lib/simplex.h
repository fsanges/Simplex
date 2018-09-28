/*
Copyright 2016, Blur Studio

This file is part of Simplex.

Simplex is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Simplex is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License
along with Simplex.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include <string>
#include <vector>
#include <array>
#include <utility>
#include <algorithm>
#include <unordered_map>
#include <stdint.h>
#include <math.h>

#include <algorithm>
#include <numeric>
#include <unordered_set>

#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "Eigen/Dense"


namespace simplex {
//enum ProgType {linear, spline, centripetal, bezier, circular};
enum ProgType {linear, spline};
static double const EPS = 1e-6;
static int const ULPS = 4;
static double const MAXVAL = 1.0; // max clamping value

class ShapeBase {
	protected:
		void *shapeRef; // pointer to whatever the user wants
		std::string name;
		size_t index;
	public:
		explicit ShapeBase(const string &name, size_t index): name(name), index(index), shapeRef(nullptr) {}
		explicit ShapeBase(const string &name): name(name), index(0u), shapeRef(nullptr) {}
		const std::string* getName() const {return &name;}
		const size_t getIndex() const { return index; }
		void setUserData(void *data) {shapeRef = data;}
		void* getUserData(){return shapeRef;}
};

class Shape : public ShapeBase {
	public:
		Shape(const string &name, size_t index): ShapeBase(name, index){}
};

class Progression : public ShapeBase {
	private:
		std::vector<std::pair<Shape*, double>> pairs;
		ProgType interp;
		size_t getInterval(double tVal, const std::vector<double> &times) const;
		std::vector<std::pair<Shape*, double>> getSplineOutput(double tVal, double mul=1.0) const;
		std::vector<std::pair<Shape*, double>> getLinearOutput(double tVal, double mul=1.0) const;
	public:
		std::vector<std::pair<Shape*, double>> getOutput(double tVal, double mul=1.0) const;

		Progression::Progression(const string &name, const vector<pair<Shape*, double> > &pairs, ProgType interp):
			ShapeBase(name), pairs(pairs), interp(interp)
		{
			std::sort(pairs.begin(), pairs.end(),
				[](const pair<Slider*, double> &a, const pair<Slider*, double> &b) {a.second < b.second; } );
		}
};

class ShapeController : public ShapeBase {
	protected:
		bool enabled;
		double value;
		double multiplier;
		Progression* prog;
	public:
		ShapeController(const std::string &name, Progression* prog, size_t index):
			ShapeBase(name, index), enabled(true), value(0.0), prog(prog) {}

		void clearValue(){value = 0.0; multiplier=1.0;}
		const double getValue() const { return value; }
		const double getMultiplier() const { return multiplier; }
		void setEnabled(bool enable){enabled = enable;}
		virtual void storeValue(
				const std::vector<double> &values,
				const std::vector<double> &posValues,
				const std::vector<double> &clamped,
				const std::vector<bool> &inverses) = 0;
		void solve(std::vector<double> &accumulator) const;
};

class Slider : public ShapeController {
	public:
		Slider(const std::string &name, Progression* prog, size_t index) : ShapeController(name, prog, index){}
		void storeValue(
				const std::vector<double> &values,
				const std::vector<double> &posValues,
				const std::vector<double> &clamped,
				const std::vector<bool> &inverses){
			this->value = values[this->index];
		}
};

class Combo : public ShapeController {
	private:
		bool exact;
		std::vector<std::pair<Slider*, double>> stateList;
	public:
		void setExact(bool e){exact = e;}
		Combo(const std::string &name, Progression* prog, size_t index,
				const std::vector<std::pair<Slider*, double>> &stateList):
			ShapeController(name, prog, index), stateList(stateList){}
		void storeValue(
				const std::vector<double> &values,
				const std::vector<double> &posValues,
				const std::vector<double> &clamped,
				const std::vector<bool> &inverses);
};

class Traversal : public ShapeController {
	private:
		ShapeController *progressCtrl;
		ShapeController *multiplierCtrl;
	public:
		Traversal(const std::string &name, Progression* prog, size_t index,
				ShapeController* progressCtrl, ShapeController* multiplierCtrl):
			ShapeController(name, prog, index), progressCtrl(progressCtrl), multiplierCtrl(multiplierCtrl){}
		void storeValue(
				const std::vector<double> &values,
				const std::vector<double> &posValues,
				const std::vector<double> &clamped,
				const std::vector<bool> &inverses){
			this->value = progressCtrl->getValue();
			this->multiplier = multiplierCtrl->getValue();
		}
};

class Floater : public ShapeController {
	private:
		std::vector<std::pair<Slider*, double>> stateList;
		std::vector<double> values;
		std::vector<bool> inverses;
	public:
		friend class TriSpace; // lets the trispace set the value for this guy
		Floater(const std::string &name, Progression* progression, const std::vector<std::pair<Slider*, double>> &stateList);
		//std::vector<double> getRow(const std::vector<Slider*>& sliders) const;
};

class TriSpace {
	private:
		// Correlates the auto-generated simplex with the user-created simplices
		// resulting from the splitting procedure
		std::vector<std::pair<std::vector<int>, std::vector<std::vector<int>>>> simplexMap;
	
		std::vector<Floater *> floaters;
		static std::vector<double> barycentric(const std::vector<std::vector<double>> &simplex, const std::vector<double> &p);
		static std::vector<std::vector<double>> simplexToCorners(const std::vector<int> &simplex);
		static std::vector<int> pointToSimp(const std::vector<double> &pt);
		static std::vector<std::vector<int>> pointToAdjSimp(const std::vector<double> &pt, double eps);
		void triangulate(); // convenience function for separating the data access from the actual math
		// Code to split a list of simplices by a list of points, only used in triangulate()
		std::vector<std::vector<std::vector<double>>> splitSimps(const std::vector<std::vector<double>> &pts, const std::vector<std::vector<int>> &simps) const;

		// break down the given simplex encoding to a list of corner points for the barycentric solver and
		// a correlation of the point index to the floater index (or size_t_MAX if invalid)
		void userSimplexToCorners(
				const std::vector<int> &simplex,
				const std::vector<int> &original,
				std::vector<std::vector<double>> out,
				std::vector<size_t> floaterCorners
				) const;
	public:
		// Take the non-related floaters and group them by shared span and orthant
		static std::vector<TriSpace> buildSpaces(std::vector<Floater*> floaters);
		TriSpace(std::vector<Floater*> floaters);
		void storeValue(
				const std::vector<double> &values,
				const std::vector<double> &posValues,
				const std::vector<double> &clamped,
				const std::vector<bool> &inverses);
};






class Simplex {
	private:
		std::vector<Shape> shapes;
		std::vector<Progression> progs;
		std::vector<Slider> sliders;
		std::vector<Combo> combos;
		std::vector<Floater> floaters;
		std::vector<TriSpace> spaces;
		std::vector<Traversal> traversals;

		bool exactSolve;
		void build();
		void rectify(const std::vector<double> &rawVec, std::vector<double> &values, std::vector<double> &clamped, std::vector<bool> &inverses);
	public:
		// public variables
		bool built;
		bool loaded;
		bool hasParseError;

		std::string parseError;
		size_t parseErrorOffset;

		explicit Simplex(const std::string &json);
		explicit Simplex(const char* json);

		void clearValues();
		bool parseJSON(const std::string &json);
		bool Simplex::parseJSONv1(const rapidjson::Document &d);
		bool Simplex::parseJSONv2(const rapidjson::Document &d);

		void setExactSolve(bool exact);

		std::vector<double> solve(const std::vector<double> &vec);
};

} // end namespace simplex
