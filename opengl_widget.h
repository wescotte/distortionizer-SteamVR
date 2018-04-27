/** @file
@brief Implementation

@date 2018/04/27

@author
Eric Wescott
<wescotte@gmail.com>

This is a modification of the OSVR-Distortionizer application found at:
https://github.com/OSVR/distortionizer
*/

//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once
#include "opengl_widget.h"
#include <QGLWidget>
//#include "undistort_shader.h"

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"


enum StatusValues {
	NO_VALUE = 0,

	LEFT_EYE = 1,
	RIGHT_EYE = 2,

	GREEN = 4,
	BLUE = 8,
	RED = 16,
	FIRST_COEFFICIENT = 32,
	SECOND_COEFFICIENT = 64,
	THIRD_COEFFICIENT = 128,

	APPLY_LINEAR_TRANSFORM = 256,
	ONLY_CENTER_CORRECT = 512,
	ONLY_ASEPECT_RATIO = 1024
};


inline StatusValues operator|(StatusValues a, StatusValues b)
{
	return static_cast<StatusValues>(static_cast<int>(a) | static_cast<int>(b));
}
inline StatusValues operator^(StatusValues a, StatusValues b)
{
	return static_cast<StatusValues>(static_cast<int>(a) ^ static_cast<int>(b));
}
inline StatusValues operator&(StatusValues a, StatusValues b)
{
	return static_cast<StatusValues>(static_cast<int>(a) & static_cast<int>(b));
}
inline StatusValues operator!=(StatusValues a, StatusValues b)
{
	return static_cast<StatusValues>(static_cast<int>(a) != static_cast<int>(b));
}
inline StatusValues operator~(StatusValues a)
{
	return static_cast<StatusValues>(~static_cast<int>(a));
}
inline StatusValues operator&&(StatusValues a, StatusValues b)
{
	return static_cast<StatusValues>(static_cast<int>(a) && static_cast<int>(b));
}
// There are three different indices of refraction for the three
// different wavelengths in the head-mounted display (R, G, B).
// This is equivalent to having lenses with three different
// powers, one per color.  This produces effectively three different
// radial distortion patterns, one per color.  The distortion of
// red is the least, green next, and blue the most.
//
// The relative values (and ratios) of these distortions depends
// on the particular material that the lenses are made of, and is
// based on an emperical fit.  This means that we must determine
// each of them independently, subject to the above inequality
// constraint.
//
// The distortion is with respect to a center of projection, which
// is common for all three colors.  The first-order correction that
// can be applied assumes that there is an additional radial shift
// that is proportionaly to the square of the distance of a pixel
// from the center of projection, with the coefficient called K1:
//    RcorrR = Rinit + K1R * (Rinit * Rinit)
//    RcorrG = Rinit + K1G * (Rinit * Rinit)
//    RcorrB = Rinit + K1B * (Rinit * Rinit)
//    0 <= K1R <= K1G <= K1B
//
// We will calculate the transformed point
// by calculating the new location using the
// following formula
// x_d = distorted x; y_d = distorted y
// x_u = undistorted x; y_u = undistorted y
// x_c = center x; y_c = center y
// r = sqrt( (x_u - x_c)^2 +  (y_u - y_c)^2 )
// x_d = x_u [(1 + k1*r^2) * (x_u - x_c) / r]
// y_d = y_u [(1 + k1*r^2) * (y_u - y_c) / r]
//
// When there are nonzero coefficients for higher-order terms
// (K2 and above), the result is a fourth-order polynomial that
// is challenging to invert analytically.

class OpenGL_Widget : public QGLWidget 
{
	Q_OBJECT

public:
	OpenGL_Widget(QWidget *parent = 0);
	~OpenGL_Widget();

	public slots:

signals:

protected:
	//------------------------------------------------------
	void initializeGL();
	void paintGL();
	void resizeGL(int width, int height);
	void mousePressEvent(QMouseEvent *event);
	void mouseMoveEvent(QMouseEvent *event);
	void keyPressEvent(QKeyEvent *event);

	//------------------------------------------------------
	bool saveConfigToJson(QString filename);
	bool loadConfigFromJson(QString filename);

	//------------------------------------------------------
	// Used as options in the rendering, depending on our
	// mode.
	void drawCrossHairs();
	void drawGrid();
	void drawCircles();

	//------------------------------------------------------
	// Helper functions for the draw routines.

	/// Draw a line from the specified begin point to the
	// specified end, doing distortion correcton.  The line
	// is drawn in short segments, with the correction applied
	// to each segment endpoint.  The color index tells whether
	// we use red (0), green (1), or blue (2) correction factors.
	// It uses the specified center of projection.
	void drawCorrectedLine(QPoint begin, QPoint end,
		QPointF cop, unsigned color, StatusValues eye);
	void drawCorrectedCircle(QPointF center, float radius,
		QPointF cop, unsigned color, StatusValues eye);

	/// Draw a set of 3 colored lines from the specified begin
	// point to the specified end, doing distortion correcton.
	void drawCorrectedLines(QPoint begin, QPoint end, QPointF cop, StatusValues eye);
	void drawCorrectedCircles(QPointF center, float radius, QPointF cop, StatusValues eye);

	/// Transform the specified pixel coordinate by the
	// color-correction distortion matrix using the appropriate
	// distortion correction.  The color index tells whether
	// we use red (0), green (1), or blue (2) correction factors.
	// It uses the specified center of projection.
	QPointF transformPoint(QPointF p, QPointF cop, unsigned color, StatusValues eye, bool debug = false);

	// Set default values for center of projection
	// Also used to reset center during execution to default values
	void setDeftCOPVals();

	//Convert point from pixels to relative
	QPointF pixelToRelative(QPointF cop);
	QPoint relativeToPixel(QPointF cop);

	void shiftCoeffecientOffset(int direction);
	void adjustCoeffecients(int direction);
	void shiftCenter(int v, int h);
	void toggleLinearTransform();
	void adjustAspectRatio(int w, int h);

	// The user is possbility maintaining two centers so we need to allow them to convert the one center to intrinsics if they wamt
	void ApplyCenterToIntrinsics();		
	void ApplyIntrincstsToCenter();

	void resetCenter();
	void resetCoeffiecents();

	void loadInitalValues();

	void drawImages();
	void drawImagesOverlay();
	//void paintEvent(QPaintEvent *event);

private:
	int d_width, d_height;  //< Size of the window we're rendering into
	QPointF d_cop_l;         //< Center of projection for the left eye
	QPointF d_cop_r;         //< Center of projection for the right eye
	QPoint d_cop;            //< Center of projection for the fullscreen
	float  d_k1_red;        //< Quadratic term for distortion of red
	float  d_k1_green;      //< Quadratic term for distortion of green
	float  d_k1_blue;       //< Quadratic term for distortion of blue
	bool fullscreen;

	// Previous left/right centers which we need to store because user can toggle between using two types of centers
	// One manually adjusted and one computed using the linear transform from intrensics
	QPointF d_cop_l_Prev, d_cop_r_Prev; 
									

	bool displayOverValues;
	double NLT_Coeffecients[2][3][3];	 // Eyes, Colors, Terms
	double Centers[2][2];				 // Eyes, X/Y
	double Extrinsics[2][4][4];			// Eyes [4x4] matrix
	double Intrinsics[2][3][3];			// Eyes [3x4] matrix
	
	rapidjson::Document json;
	StatusValues status;
	double coeffecientOffset;

	bool IntrensicsMode = false;

};