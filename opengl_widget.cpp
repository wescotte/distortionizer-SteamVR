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


#include "opengl_widget.h"



#include <QtGui>
#include <QtOpenGL>
#include <QColor>
#include <QFileDialog>
#include <iostream>
#include <math.h>
#include <stdio.h>
#include <QJsonDocument> 

#ifndef GL_MULTISAMPLE
#define GL_MULTISAMPLE  0x809D
#endif

#define CONFIG_FILE "HMD_Config.json"

//----------------------------------------------------------------------
// Helper functions

OpenGL_Widget::OpenGL_Widget(QWidget *parent)
	: QGLWidget(QGLFormat(QGL::SampleBuffers), parent)
	, d_cop_l(QPoint(0, 0))
	, d_cop_r(QPoint(0, 0))
	, d_cop(QPoint(0, 0))
	, d_cop_l_Prev(QPoint(0, 0))
	, d_cop_r_Prev(QPoint(0, 0))
	, d_k1_red(0)
	, d_k1_green(0)
	, d_k1_blue(0)
	, fullscreen(false)
{
	using namespace std;
	cout << "Distortion estimation for SteamVR HMDs" << endl
		<< endl
		<< "Keyboard controls:" << endl
		<< "SPACEBAR - Toggles stauts overlay ON/OFF" << endl
		<< "ENTER KEY - Toggle Linear Transforms ON/OFF/Aspect Ratio Only/Center Only" << endl
		<< endl
		<< "Z/X: Toggle the LEFT and RIGHT eye ON/OFF when applying values" << endl
		<< "1/2/3: Toggle on/off the 1st, 2nd, and 3rd coeffiecent" << endl
		<< "Q/W/E: Toggle on/off the GREEN, BLUE, and RED colors" << endl
		<< "LEFT and RIGHT arrow key: DECREASE and INCREASE the offset value to be applied" << endl
		<< "UP and DOWN arrow keys: INCREASE and DECREASE all active options values" << endl
		<< endl
		<< "SHIFT + arrow keys: Move the center of projection by one pixel" << endl
		<< "CONTROL + arrow keys: Aspect Ratio (LEFT/RIGHT hortizinal UP/DOWN vertical)" << endl
		<< "I - Apply center correction to Intrensics" << endl
		<< "NOTE: You can adjust the center without changing the Intrensics so you have to use \"I\" to actually apply these values" << endl
		<< endl
		<< "G - Reset recenter for active eye" << endl
		<< "H - Reset coeffiecents to 0.0 for all active eyes/colors/coefficents" << endl
		<< "J - Reset aspect ratio to 0.0 for all active eyes" << endl
		<< endl
		<< "S/L: Save/Load state from JSON config file (" << CONFIG_FILE << ")" << endl
		<< "ESCAPE: Quit the application" << endl
		<< endl;

	displayOverValues = false;

	for (int eye = 0; eye < 2; eye++)
		for (int color = 0; color < 3; color++)
			for (int term = 0; term < 3; term++)
				NLT_Coeffecients[eye][color][term] = 0.0;

	coeffecientOffset = 0.001;

	// Set default settings
	// TODO: The Intrinsics isn't quite working right yet so it's disabled by default
	status = LEFT_EYE | RIGHT_EYE | GREEN | BLUE | RED | FIRST_COEFFICIENT | SECOND_COEFFICIENT | THIRD_COEFFICIENT; // | APPLY_LINEAR_TRANSFORM;

	// Load inital values from JSON file
	QTimer::singleShot(0, [=] { loadInitalValues(); });
}

OpenGL_Widget::~OpenGL_Widget()
{
}

void OpenGL_Widget::initializeGL()
{
	qglClearColor(Qt::black);

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glShadeModel(GL_SMOOTH);
	glEnable(GL_LIGHTING);
	glEnable(GL_LIGHT0);
	glEnable(GL_MULTISAMPLE);
	static GLfloat lightPosition[4] = { 0.5, 5.0, 7.0, 1.0 };
	glLightfv(GL_LIGHT0, GL_POSITION, lightPosition);

	// Makes the colors for the primitives be what we want.
	glDisable(GL_LIGHTING);

}

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
// We can solve for a pre-distorted position that would produce
// a desired end location after passing through the optical system
// by solving for Rinit in the above equations (here done for a
// single color):
//    K1 * Rinit^2 + Rinit - Rcorr = 0
//    Solving for the positive root of the quadratic equation
//       A = K1, B = 1, C = -Rcorr
//    Rinit = (-B + sqrt(B^2 - 4AC)) / 2A
//          = (-1 + sqrt(1 + 4*K1*Rcorr)) / (2*K1)
//    K1 > 0

QPointF OpenGL_Widget::transformPoint(QPointF p, QPointF cop, unsigned color, StatusValues eye, bool debug)
{
	QPointF ret = p;

	// SteamVR has three distortion components
	// Two linear (Intrinsics, and Extrinsics) and the non linear inverse radial distortion. 
	// The Intrinsics allows you to adjust the center and aspect ratio of each dimention.
	// We adjust the center in setDeftCOPVals() but here is where we adjust the aspect ratios
	// TODO: Impliment the Extrinsics
	if ((status & APPLY_LINEAR_TRANSFORM) == APPLY_LINEAR_TRANSFORM || ( status & ONLY_ASEPECT_RATIO) == ONLY_ASEPECT_RATIO) {
		double x, y;
		double rX, rY;
		x = p.x();
		y = p.y();

		rX = cop.x() - x;
		rY = cop.y() - y;

		if (eye == LEFT_EYE) {
			x =cop.x() - (rX*Intrinsics[0][0][0]);
			y =cop.y() - (rY*Intrinsics[0][1][1]);
		}
		else {
			x = cop.x() - (rX*Intrinsics[1][0][0]);
			y = cop.y() - (rY*Intrinsics[1][1][1]);
		}
		ret.setX(x);
		ret.setY(y);
	}


	// Non linear transform
	// Formula for reversing the lens distortion obtained form Wikipeida as it is slightly different than what the
	// original OSVR Distortionizer application was using. 
	//	https://en.wikipedia.org/wiki/Distortion_(optics)#Software_correction

	// TODO: SteamVR has a "type": "DISTORT_DPOLY3" paramater for each eye's "distortion", "distortion_blue", "distortion_red" section
	//		and I believe this informs SteamVR to use a different algorithm... So in the future might need to check this value
	//		to ensure the proper algorithm is being used.

	QPointF offset = ret - cop;
	double r2 = offset.x() * offset.x() + offset.y() * offset.y();
	double r = sqrt(r2);
	double k1, k2, k3;

	switch (color) {
	case 0:
		k1 = d_k1_red;
		if (eye == LEFT_EYE) {
			k1 = NLT_Coeffecients[0][2][0];
			k2 = NLT_Coeffecients[0][2][1];
			k3 = NLT_Coeffecients[0][2][2];
		}
		else {
			k1 = NLT_Coeffecients[1][2][0];
			k2 = NLT_Coeffecients[1][2][1];
			k3 = NLT_Coeffecients[1][2][2];
		}
		break;
	case 1:
		k1 = d_k1_green;
		if (eye == LEFT_EYE) {
			k1 = NLT_Coeffecients[0][0][0];
			k2 = NLT_Coeffecients[0][0][1];
			k3 = NLT_Coeffecients[0][0][2];
		}
		else {
			k1 = NLT_Coeffecients[1][0][0];
			k2 = NLT_Coeffecients[1][0][1];
			k3 = NLT_Coeffecients[1][0][2];
		}
		break;
	case 2:
		k1 = d_k1_blue;
		if (eye == LEFT_EYE) {
			k1 = NLT_Coeffecients[0][1][0];
			k2 = NLT_Coeffecients[0][1][1];
			k3 = NLT_Coeffecients[0][1][2];
		}
		else {
			k1 = NLT_Coeffecients[1][1][0];
			k2 = NLT_Coeffecients[1][1][1];
			k3 = NLT_Coeffecients[1][1][2];
		}
		break;
	}

	// Normilze the constants..
	// TODO: Figure out the / 4.0 and the 16 constant in K1 and why this was used in the original OSVR distortionizer
	//			1/4 * 1/4 * 16 = 1 so it cancels....
	k1 = k1 / ((d_width / 4.0)*(d_width / 4.0) * 16);
	k2 = k2 / pow(d_width, 4);// *pow(4.0, 4);
	k3 = k3 / pow(d_width, 6);// *pow(4.0, 6);

	double c1, c2, c3;

	c1 = k1 * pow(r,2);
	c2 = k2 * pow(r, 4);
	c3 = k3 * pow(r, 6);
	double k = 1/( 1 + c1 + c2 + c3);

	ret = cop + (k * offset);



	// Cull the two eyes so any drawings on one doesn't overlap with the other. 
	// Not very inteligent and justs draws the point outside the screen boundry for now.
	if (eye == LEFT_EYE) {
		if (ret.x() < 0 || ret.x() > d_width / 2)
			ret.setX(-1);
		if (ret.y() < 0 || ret.y() > d_height)
			ret.setY(-1);
	}

	if (eye == RIGHT_EYE) {
		if (ret.x() > d_width || ret.x() < d_width / 2)
			ret.setX(d_width + 1);
		if (ret.y() < 0 || ret.y() > d_height)
			ret.setY(-1);
	}
	return ret;
}


void OpenGL_Widget::drawCorrectedLine(QPoint begin, QPoint end,
	QPointF cop, unsigned color, StatusValues eye)
{
	QPointF offset = end - begin;
	float len = sqrt(offset.x() * offset.x() + offset.y() * offset.y());
	QPointF offset_dir = offset / len;
	glBegin(GL_LINE_STRIP);
	for (float s = 0; s <= len; s++) {
		QPointF p = begin + s * offset_dir;
		QPointF tp = transformPoint(p, cop, color, eye);

		// Enable culling... Don't draw any vertices outside of the screen area
		if (tp.x() > -1 && tp.x() < d_width+1 && tp.y() > -1 && tp.y() < d_height+1)
			glVertex2f(tp.x(), tp.y());
	}
	glEnd();
}

void OpenGL_Widget::drawCorrectedCircle(QPointF center, float radius,
	QPointF cop, unsigned color, StatusValues eye)
{
	glBegin(GL_LINE_STRIP);
	float step = 1 / radius;
	for (float r = 0; r <= 2 * M_PI; r += step) {
		QPointF p(center.x() + radius * cos(r),
			center.y() + radius * sin(r));

		QPointF tp = transformPoint(p, cop, color, eye);
		// Enable culling... Don't draw any vertices outside of the screen area
		if (tp.x() > -1 && tp.x() < d_width+1 && tp.y() > -1 && tp.y() < d_height+1)
			glVertex2f(tp.x(), tp.y());
	}
	glEnd();
}

void OpenGL_Widget::drawCorrectedLines(QPoint begin, QPoint end, QPointF cop, StatusValues eye)
{
	float bright = 0.5f;
	glColor3f(bright, 0.0, 0.0);
	drawCorrectedLine(begin, end, cop, 0, eye);

	glColor3f(0.0, bright, 0.0);
	drawCorrectedLine(begin, end, cop, 1, eye);

	glColor3f(0.0, 0.0, bright);
	drawCorrectedLine(begin, end, cop, 2, eye);
}

void OpenGL_Widget::drawCorrectedCircles(QPointF center, float radius, QPointF cop, StatusValues eye)
{
	float bright = 0.5f;
	glColor3f(bright, 0.0, 0.0);
	drawCorrectedCircle(center, radius, cop, 0, eye);

	glColor3f(0.0, bright, 0.0);
	drawCorrectedCircle(center, radius, cop, 1, eye);

	glColor3f(0.0, 0.0, bright);
	drawCorrectedCircle(center, radius, cop, 2, eye);
}

void OpenGL_Widget::drawCrossHairs()
{
	// Draw two perpendicular lines through the center of
	// projection on the left eye, and the right eye.
	glColor3f(0.0, 1.0, 0.0);

	glBegin(GL_LINES);
	glVertex2f(0, d_cop_l.y());
	glVertex2f(d_width / 2, d_cop_l.y());
	glVertex2f(d_cop_l.x(), 0);
	glVertex2f(d_cop_l.x(), d_height);

	glVertex2f(d_width / 2, d_cop_r.y());
	glVertex2f(d_width, d_cop_r.y());
	glVertex2f(d_cop_r.x(), 0);
	glVertex2f(d_cop_r.x(), d_height);
	glEnd();

}

void OpenGL_Widget::drawGrid()
{
	// Draw a set of vertical grid lines to the right and left
	// of the center of projection for each eye.  Draw a red,
	// green, and blue line at each location with less than
	// full brightness.  Draw from the top of the screen to
	// the bottom.
	int spacing = 40;

	// Vertical lines
	// Left Eye - Right side of mid point
	for (int r = spacing; (d_cop_l.x() + r) < (d_width / 2); r += spacing) {
		QPoint begin(d_cop_l.x() + r, 0);
		QPoint end(d_cop_l.x() + r, d_height - 1);
		drawCorrectedLines(begin, end, d_cop_l, LEFT_EYE);
	}
	// Left Eye - Left side of mid point
	for (int r = spacing; (d_cop_l.x() - r) > 0; r += spacing) {
		QPoint begin(d_cop_l.x() - r, 0);
		QPoint end(d_cop_l.x() - r, d_height - 1);
		drawCorrectedLines(begin, end, d_cop_l, LEFT_EYE);
	}
	// Right Eye - Right side of mid point
	for (int r = spacing; (d_cop_r.x() + r) < (d_width); r += spacing) {
		QPoint begin(d_cop_r.x() + r, 0);
		QPoint end(d_cop_r.x() + r, d_height - 1);
		drawCorrectedLines(begin, end, d_cop_r, RIGHT_EYE);
	}
	// Right Eye - Left side of mid point
	for (int r = spacing; (d_cop_r.x() - r) > (d_width / 2); r += spacing) {
		QPoint begin(d_cop_r.x() - r, 0);
		QPoint end(d_cop_r.x() - r, d_height - 1);
		drawCorrectedLines(begin, end, d_cop_r, RIGHT_EYE);
	}


	// Horizontal lines
	// Left Eye - Top of mid point
	for (int r = spacing; (d_cop_l.y() - r) > 0; r += spacing) {
		QPoint begin(0, d_cop_l.y() - r);
		QPoint end(d_width/2, d_cop_l.y() - r);
		drawCorrectedLines(begin, end, d_cop_l, LEFT_EYE);
	}
	// Left Eye - Bottom of mid point
	for (int r = spacing; (d_cop_l.y() + r) < d_height; r += spacing) {
		QPoint begin(0, d_cop_l.y() + r);
		QPoint end(d_width/2, d_cop_l.y() + r);
		drawCorrectedLines(begin, end, d_cop_l, LEFT_EYE);
	}
	// Right Eye - Top of mid point
	for (int r = spacing; (d_cop_r.y() - r) > 0; r += spacing) {
		QPoint begin(d_width/2, d_cop_r.y() - r);
		QPoint end(d_width, d_cop_r.y() - r);
		drawCorrectedLines(begin, end, d_cop_r, RIGHT_EYE);
	}
	// Right Eye - Bottom of mid point
	for (int r = spacing; (d_cop_r.y() + r) < d_height; r += spacing) {
		QPoint begin(d_width/2, d_cop_r.y() + r);
		QPoint end(d_width, d_cop_r.y() + r);
		drawCorrectedLines(begin, end, d_cop_r, RIGHT_EYE);
	}
}

void OpenGL_Widget::drawCircles()
{

	drawCorrectedCircles(d_cop_l, 0.1 * d_width / 4, d_cop_l, LEFT_EYE);
	drawCorrectedCircles(d_cop_r, 0.1 * d_width / 4, d_cop_r, RIGHT_EYE);
	drawCorrectedCircles(d_cop_l, 0.3 * d_width / 4, d_cop_l, LEFT_EYE);
	drawCorrectedCircles(d_cop_r, 0.3 * d_width / 4, d_cop_r, RIGHT_EYE);
	drawCorrectedCircles(d_cop_l, 0.7 * d_width / 4, d_cop_l, LEFT_EYE);
	drawCorrectedCircles(d_cop_r, 0.7 * d_width / 4, d_cop_r, RIGHT_EYE);
}

void OpenGL_Widget::paintGL()
{
	qglClearColor(Qt::black);

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glLoadIdentity();
	glTranslatef(0.0, 0.0, -10.0);

	// Set up rendering state.
	// Turn on blending, so that we'll get white
	// lines when we draw three different-colored lines
	// in the same location.  Also turn off Z-buffer
	// test so we get all of the lines drawn.  Also,
	// turn off texture-mapping.
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_TEXTURE_2D);


	drawCrossHairs();
	drawGrid();
	drawCircles();

	//if (!IntrensicsMode) {
	//	printf("here");
	//	drawCrossHairs();
	//	drawGrid();
	//	drawCircles();
	//}
	//else {
	//	drawImages();
	//	drawImagesOverlay();
	//}

	// Draw text overlays
	// TODO: Figure out why the hell the QPainter is filling the screen with white making the overlay aspect not really work...
	if (displayOverValues) {
		QPainter painter(this);

		painter.setPen(Qt::black);
		painter.setFont(QFont("Helvetica", 20));
		painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);

		int ltX = 0, ltY = 0;
		int rtX = (d_width / 2) + 1;
		int rtY = 0;
		ltX = d_cop_l.x(); 		ltY = d_cop_l.y();
		rtX = d_cop_r.x();		rtY = d_cop_r.y();


		int xOffset = -250;
		int yOffset = 0;
		char msg[1024];

		// Applying to Eyes:
		if ((status & LEFT_EYE) == LEFT_EYE && (status & RIGHT_EYE) == RIGHT_EYE)
			sprintf(msg, "APPLYING TO: BOTH EYES");
		else if ((status & LEFT_EYE) == LEFT_EYE && (status & RIGHT_EYE) != RIGHT_EYE)
			sprintf(msg, "APPLYING TO: LEFT EYE ONLY");
		else if ((status & LEFT_EYE) != LEFT_EYE && (status & RIGHT_EYE) == RIGHT_EYE)
			sprintf(msg, "APPLYING TO: RIGHT EYE ONLY");
		else
			sprintf(msg, "APPLYING TO: NO EYES");
		painter.drawText(ltX + xOffset, ltY	+ yOffset, msg);
		painter.drawText(rtX + xOffset, rtY + yOffset, msg);
		yOffset = yOffset + 50;


		// Applying to Cofficients:
		sprintf(msg, "Offset Amount: %11.10f", coeffecientOffset);

		painter.drawText(ltX + xOffset, ltY + yOffset, msg);
		painter.drawText(rtX + xOffset, rtY + yOffset, msg);
		yOffset = yOffset + 50;

		sprintf(msg, "Modifying Coeffiecients: ");
		if ((status & FIRST_COEFFICIENT) == FIRST_COEFFICIENT)
			sprintf(msg, "%sFIRST\t",msg);
		if ((status & SECOND_COEFFICIENT) == SECOND_COEFFICIENT)
			sprintf(msg, "%sSECOND\t", msg);
		if ((status & THIRD_COEFFICIENT) == THIRD_COEFFICIENT)
			sprintf(msg, "%sTHIRD\t", msg);

		painter.drawText(ltX + xOffset, ltY + yOffset, msg);
		painter.drawText(rtX + xOffset, rtY + yOffset, msg);
		yOffset = yOffset + 50;


		// Applying to colors:
		sprintf(msg, "Modifying Color: ");
		if ((status & GREEN) == GREEN)
			sprintf(msg, "%sGREEN\t", msg);
		if ((status & BLUE) == BLUE)
			sprintf(msg, "%sBLUE\t", msg);
		if ((status & RED) == RED)
			sprintf(msg, "%sRED\t", msg);

		painter.drawText(ltX + xOffset, ltY + yOffset, msg);
		painter.drawText(rtX + xOffset, rtY + yOffset, msg);
		yOffset = yOffset + 50;


		// Performing linear transform?
		sprintf(msg, "Linear Transform Applied: ");
		if ((status & APPLY_LINEAR_TRANSFORM) == APPLY_LINEAR_TRANSFORM) 
			sprintf(msg, "%s\tBOTH", msg);
		else if ((status & ONLY_CENTER_CORRECT) == ONLY_CENTER_CORRECT) 
			sprintf(msg, "%s\tCenter Only", msg);
		else if ((status & ONLY_ASEPECT_RATIO) == ONLY_ASEPECT_RATIO) 
			sprintf(msg, "%s\tAspect Ratio Only", msg);
		else
			sprintf(msg, "%s\tNone", msg);

		painter.drawText(ltX + xOffset, ltY + yOffset, msg);
		painter.drawText(rtX + xOffset, rtY + yOffset, msg);
		yOffset = yOffset + 50;


		sprintf(msg, "------------- LEFT EYE -------------     ------------- RIGHT EYE -------------\n" );
		painter.drawText(ltX + xOffset, ltY + yOffset, msg);
		painter.drawText(rtX + xOffset, rtY + yOffset, msg);
		yOffset = yOffset + 50;

		sprintf(msg, "        %-13s%-13s%-13s%-13s%-13s%-13s\n", "GREEN", "BLUE", "RED", "GREEN", "BLUE", "RED");
		painter.drawText(ltX + xOffset, ltY + yOffset, msg);
		painter.drawText(rtX + xOffset, rtY + yOffset, msg);
		yOffset = yOffset + 50;

		sprintf(msg, "coeff1: %-13.8g%-13.8g%-13.8g%-13.8g%-13.8g%-13.8g\n", NLT_Coeffecients[0][0][0], NLT_Coeffecients[0][1][0],
			NLT_Coeffecients[0][2][0], NLT_Coeffecients[1][0][0], NLT_Coeffecients[1][1][0], NLT_Coeffecients[1][2][0]);
		painter.drawText(ltX + xOffset, ltY + yOffset, msg);
		painter.drawText(rtX + xOffset, rtY + yOffset, msg);
		yOffset = yOffset + 50;

		sprintf(msg, "coeff1: %-13.8g%-13.8g%-13.8g%-13.8g%-13.8g%-13.8g\n", NLT_Coeffecients[0][0][1], NLT_Coeffecients[0][1][1],
			NLT_Coeffecients[0][2][1], NLT_Coeffecients[1][0][1], NLT_Coeffecients[1][1][1], NLT_Coeffecients[1][2][1]);
		painter.drawText(ltX + xOffset, ltY + yOffset, msg);
		painter.drawText(rtX + xOffset, rtY + yOffset, msg);
		yOffset = yOffset + 50;

		sprintf(msg, "coeff1: %-13.8g%-13.8g%-13.8g%-13.8g%-13.8g%-13.8g\n", NLT_Coeffecients[0][0][2], NLT_Coeffecients[0][1][2],
			NLT_Coeffecients[0][2][2], NLT_Coeffecients[1][0][2], NLT_Coeffecients[1][1][2], NLT_Coeffecients[1][2][2]);
		painter.drawText(ltX + xOffset, ltY + yOffset, msg);
		painter.drawText(rtX + xOffset, rtY + yOffset, msg);
		yOffset = yOffset + 50;

		sprintf(msg, "Center X: %-13.8g Center Y: %-13.8g     Center X: %-13.8g Center Y: %-13.8g"
			, Intrinsics[0][0][2], Intrinsics[0][1][2], Intrinsics[1][0][2], Intrinsics[1][1][2]);
		painter.drawText(ltX + xOffset, ltY + yOffset, msg);
		painter.drawText(rtX + xOffset, rtY + yOffset, msg);
		yOffset = yOffset + 50;

		sprintf(msg, "Aspect X: %-13.8g Aspect Y: %-13.8g     Aspect X: %-13.8g Aspect Y: %-13.8g"
			, Intrinsics[0][0][0], Intrinsics[0][1][1], Intrinsics[1][0][0], Intrinsics[1][1][1]);
		painter.drawText(ltX + xOffset, ltY + yOffset, msg);
		painter.drawText(rtX + xOffset, rtY + yOffset, msg);
		yOffset = yOffset + 50;
		painter.end();
	}
}


void OpenGL_Widget::resizeGL(int width, int height)
{
	d_width = width;
	d_height = height;
	glViewport(0, 0, width, height);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	// Make the window one unit high (-0.5 to 0.5) and have an aspect ratio that matches
	// the aspect ratio of the window.  We also make the left side of the window be at
	// the origin.
	float aspect;
	if ((height <= 0) || (width < 0)) {
		aspect = 1.0;
	}
	else {
		aspect = static_cast<float>(width) / height;
	}
	glOrtho(0, d_width - 1, 0, d_height - 1, 5.0, 15.0);
	glMatrixMode(GL_MODELVIEW);

	// Hack... For some reasons on some systems this is being called for each monitor instead of just the one we specify...
	// Now for debugging purposes I still want this to work on a regular machine w/o a Vive. 
	// However, if that monitor is detected first (before the Vive) then it screws up the center positions for the Vive screen...
	// So if we detect a Vive resolution then we just force it to use that one instead.
	// This way we can debug on a regular screen and still ensure the Vive works
	if (width == 2160 && height == 1200) {
		d_cop_l_Prev = QPointF(0, 0);
		d_cop_r_Prev = QPointF(0, 0);
	}
	setDeftCOPVals();
}

void OpenGL_Widget::setDeftCOPVals() {
	// TODO: This is VERY clunky... Figure out a better way to handle the center shifts.

	// Set intial center values
	if (d_cop_l_Prev == QPointF(0,0) && d_cop_r_Prev == QPointF(0,0) ) {
		// Default center of projection is the center of the left half
		// of the screen.
		d_cop_l.setX(d_width / 4);
		d_cop_l.setY(d_height / 2);

		// Find the mirror of the left-eye's center of projection
		// around the screen center to find the right eye's COP.
		d_cop_r = QPoint(d_width - d_cop_l.x(), d_cop_l.y());

		d_cop_l_Prev = d_cop_l;
		d_cop_r_Prev = d_cop_r;
	}
	else if ((status & APPLY_LINEAR_TRANSFORM) == APPLY_LINEAR_TRANSFORM || (status & ONLY_CENTER_CORRECT) == ONLY_CENTER_CORRECT) {
		// Backup previous center value because now we're going to use the linear transform center
		// Only backup during the first transition though...
		if ((status & ONLY_CENTER_CORRECT) != ONLY_CENTER_CORRECT) {
			d_cop_l_Prev = d_cop_l;
			d_cop_r_Prev = d_cop_r;
		}

		double CxL, CxR, Cy;
		CxL = d_width / 4;
		CxR = d_width / 2 + CxL;
		Cy = d_height / 2;

		d_cop_l.setX(CxL + (CxL * Intrinsics[0][0][2]));
		d_cop_l.setY(Cy + (Cy * Intrinsics[0][1][2]));
												  
		d_cop_r.setX(CxR + (CxR * Intrinsics[1][0][2]));
		d_cop_r.setY(Cy + (Cy * Intrinsics[1][1][2]));
	}
	else {
		// Restore previous center value because we are no longer using linear transform for center
		d_cop_l = d_cop_l_Prev;
		d_cop_r = d_cop_r_Prev;
	}

}

void OpenGL_Widget::keyPressEvent(QKeyEvent *event)
{
	StatusValues toggle = NO_VALUE;

	switch (event->key()) {
	case Qt::Key_Escape:
		QApplication::quit();
		break;
	case Qt::Key_S: // Save the state to an output file.
					// XXX Would like to throw a dialog box, but it shows in HMD
					// and cannot be moved.
		saveConfigToJson(CONFIG_FILE);
		break;
	case Qt::Key_L: // Load the state from an output file.
					// XXX Would like to throw a dialog box, but it shows in HMD
					// and cannot be moved.
		loadConfigFromJson(CONFIG_FILE);
		break;

	// Toggle coeffiecents
	// TODO: Broken... For some reason this also is toggling the APPLY_LINEAR_TRANSFORM and I don't know why yet... Investigate

	//case '`':	// Can't find the damn Qt::Key_XXXX for a Tilde!
	//	toggle = FIRST_COEFFICIENT | SECOND_COEFFICIENT | THIRD_COEFFICIENT;
	//	status = status ^ toggle;
	//	if ( (status & toggle) != NO_VALUE)
	//		status = status | toggle;
	//	else
	//		status = status | (~toggle);
		break;
	case Qt::Key_1:
		status = status ^ FIRST_COEFFICIENT;
		break;
	case Qt::Key_2:
		status = status ^ SECOND_COEFFICIENT;
		break;
	case Qt::Key_3:
		status = status ^ THIRD_COEFFICIENT;
		break;

	// Toggle Colors
	// TODO: Broken... For some reason this also is toggling the APPLY_LINEAR_TRANSFORM and I don't know why yet... Investigate

	//case Qt::Key_Tab:
	//	toggle = GREEN | BLUE | RED;
	//	status = status ^ toggle;
	//	if ( (status & toggle) != NO_VALUE)
	//		status = status | toggle;
	//	else
	//		status = status | (~toggle);
	//	break;
	case Qt::Key_Q:
		status = status ^ GREEN;
		break;
	case Qt::Key_W:
		status = status ^ BLUE;
		break;
	case Qt::Key_E:
		status = status ^ RED;
		break;
	
	// Toggle Eyes
	case Qt::Key_Z:
		status = status ^ LEFT_EYE;
		break;
	case Qt::Key_X:
		status = status ^ RIGHT_EYE;
		break;

	// Toggle linear transform on/off/partial
	case Qt::Key_Enter:
	case Qt::Key_Return:
		toggleLinearTransform();
		break;


	// Adjust modification value or adjust center
	case Qt::Key_Left:
		if (event->modifiers() & Qt::ShiftModifier)
			shiftCenter(0, -1);
		else if (event->modifiers() & Qt::ControlModifier)
			adjustAspectRatio(-1, 0);
		else
			shiftCoeffecientOffset(-1);
		break;
	case Qt::Key_Right:
		if (event->modifiers() & Qt::ShiftModifier) 
			shiftCenter(0, 1);
		else if (event->modifiers() & Qt::ControlModifier)
			adjustAspectRatio(1, 0);
		else
			shiftCoeffecientOffset(1);
		break;
	case Qt::Key_Down:
		if (event->modifiers() & Qt::ShiftModifier) 
			shiftCenter(1, 0);
		else if (event->modifiers() & Qt::ControlModifier)
			adjustAspectRatio(0,-1);
		else
			adjustCoeffecients(-1);
		break;
	case Qt::Key_Up:
		if (event->modifiers() & Qt::ShiftModifier) 
				shiftCenter(-1, 0);
		else if (event->modifiers() & Qt::ControlModifier)
			adjustAspectRatio(0,1);
		else
			adjustCoeffecients(1);
		break;


	case Qt::Key_I:
		ApplyCenterToIntrinsics();
		break;

	// Toggle Text overlay
	case Qt::Key_Space:
		if (displayOverValues)
			displayOverValues = false;
		else
			displayOverValues = true;
		break;

	// Reset values
	case Qt::Key_G:
		resetCenter();
		break;
	case Qt::Key_H:
		adjustCoeffecients(0);
// Added reset functionality to the adjustCoeffecients function so don't need a dedicated reset anymore.
// TODO: Might want to repurpose the reset so it 0's out all regardless of what is "active"
//		resetCoeffiecents();
		break;
	case Qt::Key_J:
		adjustAspectRatio(-2, -2);
		break;
	}

	updateGL();
}



void OpenGL_Widget::mousePressEvent(QMouseEvent *event)
{
}

void OpenGL_Widget::mouseMoveEvent(QMouseEvent *event)
{

	if (event->buttons() & Qt::LeftButton) {
		// XXX
	}
	else if (event->buttons() & Qt::RightButton) {
		// XXX
	}

}

QPointF OpenGL_Widget::pixelToRelative(QPointF cop) {
	QPointF relative_cop;
	float cop_x, cop_y;
	cop_x = (float)cop.x() / (float)d_width;
	cop_y = (float)cop.y() / (float)d_height;
	relative_cop.setX(cop_x);
	relative_cop.setY(cop_y);

	return relative_cop;
}

QPoint OpenGL_Widget::relativeToPixel(QPointF cop) {

	QPoint pixel_cop;
	int cop_x, cop_y;
	cop_x = cop.x() * d_width;
	cop_y = cop.y() * d_height;
	pixel_cop.setX(cop_x);
	pixel_cop.setY(cop_y);

	return pixel_cop;
}


bool OpenGL_Widget::saveConfigToJson(QString filename)
{
	// Todo... Add some basic error checking

	// Left Eye
	json["tracking_to_eye_transform"][0]["distortion"]["center_x"].SetDouble(Centers[0][1]);
	json["tracking_to_eye_transform"][0]["distortion"]["center_y"].SetDouble(Centers[0][2]);

	// Intrinsics
	json["tracking_to_eye_transform"][0]["intrinsics"][0][0].SetDouble(Intrinsics[0][0][0]);
	json["tracking_to_eye_transform"][0]["intrinsics"][0][1].SetDouble(Intrinsics[0][0][1]);
	json["tracking_to_eye_transform"][0]["intrinsics"][0][2].SetDouble(Intrinsics[0][0][2]);
	json["tracking_to_eye_transform"][0]["intrinsics"][1][0].SetDouble(Intrinsics[0][1][0]);
	json["tracking_to_eye_transform"][0]["intrinsics"][1][1].SetDouble(Intrinsics[0][1][1]);
	json["tracking_to_eye_transform"][0]["intrinsics"][1][2].SetDouble(Intrinsics[0][1][2]);
	json["tracking_to_eye_transform"][0]["intrinsics"][2][0].SetDouble(Intrinsics[0][2][0]);
	json["tracking_to_eye_transform"][0]["intrinsics"][2][1].SetDouble(Intrinsics[0][2][1]);
	json["tracking_to_eye_transform"][0]["intrinsics"][2][2].SetDouble(Intrinsics[0][2][2]);

	// Green
	json["tracking_to_eye_transform"][0]["distortion"]["coeffs"][0].SetDouble(NLT_Coeffecients[0][0][0]);
	json["tracking_to_eye_transform"][0]["distortion"]["coeffs"][1].SetDouble(NLT_Coeffecients[0][0][1]);
	json["tracking_to_eye_transform"][0]["distortion"]["coeffs"][2].SetDouble(NLT_Coeffecients[0][0][2]);
	json["tracking_to_eye_transform"][0]["distortion"]["center_x"].SetDouble(Intrinsics[0][0][2]);
	json["tracking_to_eye_transform"][0]["distortion"]["center_y"].SetDouble(Intrinsics[0][1][2]);

	// Blue
	json["tracking_to_eye_transform"][0]["distortion_blue"]["coeffs"][0].SetDouble(NLT_Coeffecients[0][1][0]);
	json["tracking_to_eye_transform"][0]["distortion_blue"]["coeffs"][1].SetDouble(NLT_Coeffecients[0][1][1]);
	json["tracking_to_eye_transform"][0]["distortion_blue"]["coeffs"][2].SetDouble(NLT_Coeffecients[0][1][2]);
	json["tracking_to_eye_transform"][0]["distortion_blue"]["center_x"].SetDouble(Intrinsics[0][0][2]);
	json["tracking_to_eye_transform"][0]["distortion_blue"]["center_y"].SetDouble(Intrinsics[0][1][2]);

	// Red
	json["tracking_to_eye_transform"][0]["distortion_red"]["coeffs"][0].SetDouble(NLT_Coeffecients[0][2][0]);
	json["tracking_to_eye_transform"][0]["distortion_red"]["coeffs"][1].SetDouble(NLT_Coeffecients[0][2][1]);
	json["tracking_to_eye_transform"][0]["distortion_red"]["coeffs"][2].SetDouble(NLT_Coeffecients[0][2][2]);
	json["tracking_to_eye_transform"][0]["distortion_red"]["center_x"].SetDouble(Intrinsics[0][0][2]);
	json["tracking_to_eye_transform"][0]["distortion_red"]["center_y"].SetDouble(Intrinsics[0][1][2]);


	// Right Eye
	json["tracking_to_eye_transform"][1]["distortion"]["center_x"].SetDouble(Centers[1][1]);
	json["tracking_to_eye_transform"][1]["distortion"]["center_y"].SetDouble(Centers[1][2]);

	// Intrinsics
	json["tracking_to_eye_transform"][1]["intrinsics"][0][0].SetDouble(Intrinsics[1][0][0]);
	json["tracking_to_eye_transform"][1]["intrinsics"][0][1].SetDouble(Intrinsics[1][0][1]);
	json["tracking_to_eye_transform"][1]["intrinsics"][0][2].SetDouble(Intrinsics[1][0][2]);
	json["tracking_to_eye_transform"][1]["intrinsics"][1][0].SetDouble(Intrinsics[1][1][0]);
	json["tracking_to_eye_transform"][1]["intrinsics"][1][1].SetDouble(Intrinsics[1][1][1]);
	json["tracking_to_eye_transform"][1]["intrinsics"][1][2].SetDouble(Intrinsics[1][1][2]);
	json["tracking_to_eye_transform"][1]["intrinsics"][2][0].SetDouble(Intrinsics[1][2][0]);
	json["tracking_to_eye_transform"][1]["intrinsics"][2][1].SetDouble(Intrinsics[1][2][1]);
	json["tracking_to_eye_transform"][1]["intrinsics"][2][2].SetDouble(Intrinsics[1][2][2]);

	// Green
	json["tracking_to_eye_transform"][1]["distortion"]["coeffs"][0].SetDouble(NLT_Coeffecients[1][0][0]);
	json["tracking_to_eye_transform"][1]["distortion"]["coeffs"][1].SetDouble(NLT_Coeffecients[1][0][1]);
	json["tracking_to_eye_transform"][1]["distortion"]["coeffs"][2].SetDouble(NLT_Coeffecients[1][0][2]);
	json["tracking_to_eye_transform"][1]["distortion"]["center_x"].SetDouble(Intrinsics[1][0][2]);
	json["tracking_to_eye_transform"][1]["distortion"]["center_y"].SetDouble(Intrinsics[1][1][2]);

	// Blue
	json["tracking_to_eye_transform"][1]["distortion_blue"]["coeffs"][0].SetDouble(NLT_Coeffecients[1][1][0]);
	json["tracking_to_eye_transform"][1]["distortion_blue"]["coeffs"][1].SetDouble(NLT_Coeffecients[1][1][1]);
	json["tracking_to_eye_transform"][1]["distortion_blue"]["coeffs"][2].SetDouble(NLT_Coeffecients[1][1][2]);
	json["tracking_to_eye_transform"][1]["distortion_blue"]["center_x"].SetDouble(Intrinsics[1][0][2]);
	json["tracking_to_eye_transform"][1]["distortion_blue"]["center_y"].SetDouble(Intrinsics[1][1][2]);

	// Red
	json["tracking_to_eye_transform"][1]["distortion_red"]["coeffs"][0].SetDouble(NLT_Coeffecients[1][2][0]);
	json["tracking_to_eye_transform"][1]["distortion_red"]["coeffs"][1].SetDouble(NLT_Coeffecients[1][2][1]);
	json["tracking_to_eye_transform"][1]["distortion_red"]["coeffs"][2].SetDouble(NLT_Coeffecients[1][2][2]);
	json["tracking_to_eye_transform"][1]["distortion_red"]["center_x"].SetDouble(Intrinsics[1][0][2]);
	json["tracking_to_eye_transform"][1]["distortion_red"]["center_y"].SetDouble(Intrinsics[1][1][2]);

	QFile file(filename);
	file.open(QIODevice::WriteOnly | QIODevice::Text);

	rapidjson::StringBuffer buffer;
	buffer.Clear();

	rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
	json.Accept(writer);

	file.write(buffer.GetString());
	file.close();

	return true;
}

bool OpenGL_Widget::loadConfigFromJson(QString filename)
{
	// Todo: Add some basic error checking/sanity checking

	QString val;
	QFile file("HMD_Config.json");
	if (!file.exists())
		return false;

	file.open(QIODevice::ReadOnly | QIODevice::Text);

	val = file.readAll();
	file.close();

	using namespace rapidjson;
	
	json.Parse(val.toStdString().c_str());
	// Todo make sure all the various parts of the JSON file actually exist....

	// Left Eye
	Centers[0][1] = json["tracking_to_eye_transform"][0]["distortion"]["center_x"].GetDouble();
	Centers[0][2] = json["tracking_to_eye_transform"][0]["distortion"]["center_y"].GetDouble();

	Intrinsics[0][0][0] = json["tracking_to_eye_transform"][0]["intrinsics"][0][0].GetDouble();
	Intrinsics[0][0][1] = json["tracking_to_eye_transform"][0]["intrinsics"][0][1].GetDouble();
	Intrinsics[0][0][2] = json["tracking_to_eye_transform"][0]["intrinsics"][0][2].GetDouble();
	Intrinsics[0][1][0] = json["tracking_to_eye_transform"][0]["intrinsics"][1][0].GetDouble();
	Intrinsics[0][1][1] = json["tracking_to_eye_transform"][0]["intrinsics"][1][1].GetDouble();
	Intrinsics[0][1][2] = json["tracking_to_eye_transform"][0]["intrinsics"][1][2].GetDouble();
	Intrinsics[0][2][0] = json["tracking_to_eye_transform"][0]["intrinsics"][2][0].GetDouble();
	Intrinsics[0][2][1] = json["tracking_to_eye_transform"][0]["intrinsics"][2][1].GetDouble();
	Intrinsics[0][2][2] = json["tracking_to_eye_transform"][0]["intrinsics"][2][2].GetDouble();

	// Green
	NLT_Coeffecients[0][0][0] = json["tracking_to_eye_transform"][0]["distortion"]["coeffs"][0].GetDouble();
	NLT_Coeffecients[0][0][1] = json["tracking_to_eye_transform"][0]["distortion"]["coeffs"][1].GetDouble();
	NLT_Coeffecients[0][0][2] = json["tracking_to_eye_transform"][0]["distortion"]["coeffs"][2].GetDouble();
	// Blue
	NLT_Coeffecients[0][1][0] = json["tracking_to_eye_transform"][0]["distortion_blue"]["coeffs"][0].GetDouble();
	NLT_Coeffecients[0][1][1] = json["tracking_to_eye_transform"][0]["distortion_blue"]["coeffs"][1].GetDouble();
	NLT_Coeffecients[0][1][2] = json["tracking_to_eye_transform"][0]["distortion_blue"]["coeffs"][2].GetDouble();
	// Red
	NLT_Coeffecients[0][2][0] = json["tracking_to_eye_transform"][0]["distortion_red"]["coeffs"][0].GetDouble();
	NLT_Coeffecients[0][2][1] = json["tracking_to_eye_transform"][0]["distortion_red"]["coeffs"][1].GetDouble();
	NLT_Coeffecients[0][2][2] = json["tracking_to_eye_transform"][0]["distortion_red"]["coeffs"][2].GetDouble();

	// Right Eye
	Centers[1][1] = json["tracking_to_eye_transform"][1]["distortion"]["center_x"].GetDouble();
	Centers[1][2] = json["tracking_to_eye_transform"][1]["distortion"]["center_y"].GetDouble();

	Intrinsics[1][0][0] = json["tracking_to_eye_transform"][1]["intrinsics"][0][0].GetDouble();
	Intrinsics[1][0][1] = json["tracking_to_eye_transform"][1]["intrinsics"][0][1].GetDouble();
	Intrinsics[1][0][2] = json["tracking_to_eye_transform"][1]["intrinsics"][0][2].GetDouble();
	Intrinsics[1][1][0] = json["tracking_to_eye_transform"][1]["intrinsics"][1][0].GetDouble();
	Intrinsics[1][1][1] = json["tracking_to_eye_transform"][1]["intrinsics"][1][1].GetDouble();
	Intrinsics[1][1][2] = json["tracking_to_eye_transform"][1]["intrinsics"][1][2].GetDouble();
	Intrinsics[1][2][0] = json["tracking_to_eye_transform"][1]["intrinsics"][2][0].GetDouble();
	Intrinsics[1][2][1] = json["tracking_to_eye_transform"][1]["intrinsics"][2][1].GetDouble();
	Intrinsics[1][2][2] = json["tracking_to_eye_transform"][1]["intrinsics"][2][2].GetDouble();

	// Green
	NLT_Coeffecients[1][0][0] = json["tracking_to_eye_transform"][1]["distortion"]["coeffs"][0].GetDouble();
	NLT_Coeffecients[1][0][1] = json["tracking_to_eye_transform"][1]["distortion"]["coeffs"][1].GetDouble();
	NLT_Coeffecients[1][0][2] = json["tracking_to_eye_transform"][1]["distortion"]["coeffs"][2].GetDouble();
	// Blue			 											  
	NLT_Coeffecients[1][1][0] = json["tracking_to_eye_transform"][1]["distortion_blue"]["coeffs"][0].GetDouble();
	NLT_Coeffecients[1][1][1] = json["tracking_to_eye_transform"][1]["distortion_blue"]["coeffs"][1].GetDouble();
	NLT_Coeffecients[1][1][2] = json["tracking_to_eye_transform"][1]["distortion_blue"]["coeffs"][2].GetDouble();
	// Red			 											  
	NLT_Coeffecients[1][2][0] = json["tracking_to_eye_transform"][1]["distortion_red"]["coeffs"][0].GetDouble();
	NLT_Coeffecients[1][2][1] = json["tracking_to_eye_transform"][1]["distortion_red"]["coeffs"][1].GetDouble();
	NLT_Coeffecients[1][2][2] = json["tracking_to_eye_transform"][1]["distortion_red"]["coeffs"][2].GetDouble();

	ApplyIntrincstsToCenter();
//	setDeftCOPVals();

	return true;
}


void OpenGL_Widget::shiftCoeffecientOffset(int direction)
{
	coeffecientOffset = coeffecientOffset * pow(10,direction);

	if (coeffecientOffset <= 0.0000000001)
		coeffecientOffset = 0.0000000001;

	if (coeffecientOffset >= 1.0)
		coeffecientOffset = 1;
}

void OpenGL_Widget::adjustCoeffecients(int direction) {;
	// The " NLT_Coeffecients[0][0][0] * abs(direction) " part allows us to specifiy direciton of 0 which 
	// allows us to quickly reset coeffiecients to 0

	double tCoeffecientets[2][3][3];
	for (int eye = 0; eye < 2; eye++)
		for (int col = 0; col < 3; col++)
			for (int cof = 0; cof < 3; cof++)
				tCoeffecientets[eye][col][cof] = NLT_Coeffecients[eye][col][cof];

	if ((status & LEFT_EYE) && (status & GREEN) && (status & FIRST_COEFFICIENT))
		tCoeffecientets[0][0][0] = NLT_Coeffecients[0][0][0] * abs(direction) + coeffecientOffset * direction;
	if ((status & LEFT_EYE) && (status & GREEN) && (status & SECOND_COEFFICIENT))
		tCoeffecientets[0][0][1] = NLT_Coeffecients[0][0][1] * abs(direction) + coeffecientOffset * direction;
	if ((status & LEFT_EYE) && (status & GREEN) && (status & THIRD_COEFFICIENT))
		tCoeffecientets[0][0][2] = NLT_Coeffecients[0][0][2] * abs(direction) + coeffecientOffset * direction;

	if ((status & LEFT_EYE) && (status & BLUE) && (status & FIRST_COEFFICIENT))
		tCoeffecientets[0][1][0] = NLT_Coeffecients[0][1][0] * abs(direction) + coeffecientOffset * direction;
	if ((status & LEFT_EYE) && (status & BLUE) && (status & SECOND_COEFFICIENT))
		tCoeffecientets[0][1][1] = NLT_Coeffecients[0][1][1] * abs(direction) + coeffecientOffset * direction;
	if ((status & LEFT_EYE) && (status & BLUE) && (status & THIRD_COEFFICIENT))
		tCoeffecientets[0][1][2] = NLT_Coeffecients[0][1][2] * abs(direction) + coeffecientOffset * direction;

	if ((status & LEFT_EYE) && (status & RED) && (status & FIRST_COEFFICIENT))
		tCoeffecientets[0][2][0] = NLT_Coeffecients[0][2][0] * abs(direction) + coeffecientOffset * direction;
	if ((status & LEFT_EYE) && (status & RED) && (status & SECOND_COEFFICIENT))
		tCoeffecientets[0][2][1] = NLT_Coeffecients[0][2][1] * abs(direction) + coeffecientOffset * direction;
	if ((status & LEFT_EYE) && (status & RED) && (status & THIRD_COEFFICIENT))
		tCoeffecientets[0][2][2] = NLT_Coeffecients[0][2][2] * abs(direction) + coeffecientOffset * direction;


	if ((status & RIGHT_EYE) && (status & GREEN) && (status & FIRST_COEFFICIENT))
		tCoeffecientets[1][0][0] = NLT_Coeffecients[1][0][0] * abs(direction) + coeffecientOffset * direction;
	if ((status & RIGHT_EYE) && (status & GREEN) && (status & SECOND_COEFFICIENT))
		tCoeffecientets[1][0][1] = NLT_Coeffecients[1][0][1] * abs(direction) + coeffecientOffset * direction;
	if ((status & RIGHT_EYE) && (status & GREEN) && (status & THIRD_COEFFICIENT))
		tCoeffecientets[1][0][2] = NLT_Coeffecients[1][0][2] * abs(direction) + coeffecientOffset * direction;

	if ((status & RIGHT_EYE) && (status & BLUE) && (status & FIRST_COEFFICIENT))
		tCoeffecientets[1][1][0] = NLT_Coeffecients[1][1][0] * abs(direction) + coeffecientOffset * direction;
	if ((status & RIGHT_EYE) && (status & BLUE) && (status & SECOND_COEFFICIENT))
		tCoeffecientets[1][1][1] = NLT_Coeffecients[1][1][1] * abs(direction) + coeffecientOffset * direction;
	if ((status & RIGHT_EYE) && (status & BLUE) && (status & THIRD_COEFFICIENT))
		tCoeffecientets[1][1][2] = NLT_Coeffecients[1][1][2] * abs(direction) + coeffecientOffset * direction;

	if ((status & RIGHT_EYE) && (status & RED) && (status & FIRST_COEFFICIENT))
		tCoeffecientets[1][2][0] = NLT_Coeffecients[1][2][0] * abs(direction) + coeffecientOffset * direction;
	if ((status & RIGHT_EYE) && (status & RED) && (status & SECOND_COEFFICIENT))
		tCoeffecientets[1][2][1] = NLT_Coeffecients[1][2][1] * abs(direction) + coeffecientOffset * direction;
	if ((status & RIGHT_EYE) && (status & RED) && (status & THIRD_COEFFICIENT))
		tCoeffecientets[1][2][2] = NLT_Coeffecients[1][2][2] * abs(direction) + coeffecientOffset * direction;


	// TODO: Figure this out....
	// I believe SteamVR wants the coeffecients to be ordred by magnitude...
	// So we check to ensure |K1| > |K2| > |K3|
	bool foundDiscprepancy = false;
	//for (int eye=0; eye < 2; eye++)
	//	for (int col = 0; col < 3; col++) {
	//		if ( (	abs(tCoeffecientets[eye][col][0]) < abs(tCoeffecientets[eye][col][1]) && tCoeffecientets[eye][col][0] < tCoeffecientets[eye][col][1]) ||
	//			 (	abs(tCoeffecientets[eye][col][0]) < abs(tCoeffecientets[eye][col][2]) && tCoeffecientets[eye][col][0] < tCoeffecientets[eye][col][2]) ||
	//			 (	abs(tCoeffecientets[eye][col][1]) < abs(tCoeffecientets[eye][col][2]) && tCoeffecientets[eye][col][1] < tCoeffecientets[eye][col][2]) )
	//				foundDiscprepancy = true;
	//}

	if (!foundDiscprepancy) {
		for (int eye = 0; eye < 2; eye++)
			for (int col = 0; col < 3; col++)
				for (int cof = 0; cof < 3; cof++)
					NLT_Coeffecients[eye][col][cof] = tCoeffecientets[eye][col][cof];
	}
	else {
		QApplication::beep();
	}
}


void OpenGL_Widget::shiftCenter(int v, int h)
{
	// We do something different we are adjusting the center when the intrinsics linear transform is enabled...
	if ((status & APPLY_LINEAR_TRANSFORM) == APPLY_LINEAR_TRANSFORM || (status & ONLY_CENTER_CORRECT) == ONLY_CENTER_CORRECT) {
		if ((status & LEFT_EYE) == LEFT_EYE) {
			Intrinsics[0][0][2] += coeffecientOffset * -h;
			Intrinsics[0][1][2] += coeffecientOffset * -v;
		}

		if ((status & RIGHT_EYE) == RIGHT_EYE) {
			Intrinsics[1][0][2] += coeffecientOffset * h;
			Intrinsics[1][1][2] += coeffecientOffset * -v;
		}

		setDeftCOPVals();
	}
	else {
		// To make things easier if somebody adjusts the horizontal when both eyes are enabled we shift both at the same time
		if (h != 0) {
			if ((status & LEFT_EYE) == LEFT_EYE && (status & RIGHT_EYE) == RIGHT_EYE) {
				d_cop_l.setX(d_cop_l.x() - h);
				d_cop_r.setX(d_cop_r.x() + h);
			}
			else if ((status & LEFT_EYE) == LEFT_EYE) {
				d_cop_l.setX(d_cop_l.x() + h);
			}
			else if ((status & RIGHT_EYE) == RIGHT_EYE) {
				d_cop_r.setX(d_cop_r.x() + h);
			}
		}
		if (v != 0) {
			if ((status & LEFT_EYE) == LEFT_EYE) 
				d_cop_l.setY(d_cop_l.y() + -v);
			if ((status & RIGHT_EYE) == RIGHT_EYE) 
				d_cop_r.setY(d_cop_r.y() + -v);
		}
	}
}

void OpenGL_Widget::toggleLinearTransform() {
	if ((status & APPLY_LINEAR_TRANSFORM) == APPLY_LINEAR_TRANSFORM) {
		status = status ^ APPLY_LINEAR_TRANSFORM;
		status = status ^ ONLY_CENTER_CORRECT;
	}
	else if ((status & ONLY_CENTER_CORRECT) == ONLY_CENTER_CORRECT) {
		status = status ^ ONLY_CENTER_CORRECT;
		status = status ^ ONLY_ASEPECT_RATIO;

	}
	else if ((status & ONLY_ASEPECT_RATIO) == ONLY_ASEPECT_RATIO) {
		status = status ^ ONLY_ASEPECT_RATIO;
	}
	else {
		status = status ^ APPLY_LINEAR_TRANSFORM;
	}

	setDeftCOPVals();
}


void OpenGL_Widget::adjustAspectRatio(int w, int h) {
	// The default resoltuion of the Vive is 1080 x 1200 per eye
	// The defeault aspect ratio is 1.2 for the horizontal and 1.08 for the vertical because SteamVR operates based on a square space
	// So the horizontal resolution needs to be multipled by 1.20 and the vertical by 1.08 to be square
	// 2160 / 2 * 1.20 = 1200 * 1.08

	// When calculating the default aspect ratio the width and are reversed...
	// X aspect ratio = 1200 / 1000			= 1.20
	// Y aspect ratio = 2160 / 1000 / 2		= 1.08
	if (status & LEFT_EYE) {
		if (w != -2)
			Intrinsics[0][0][0] += w * coeffecientOffset;
		else
			Intrinsics[0][0][0] = (double)d_height / 1000;
		if (h != -2)
			Intrinsics[0][1][1] += h * coeffecientOffset;
		else
			Intrinsics[0][1][1] = (double)d_width / 1000 / 2; // The frame buffer is just one screen so both eyes share the horizontal resolution
	}

	if (status & RIGHT_EYE) {
		if (w != -2)
			Intrinsics[1][0][0] += w * coeffecientOffset;
		else
			Intrinsics[1][0][0] = (double)d_height / 1000;
		if (h != -2)
			Intrinsics[1][1][1] += h * coeffecientOffset;
		else
			Intrinsics[1][1][1] = (double)d_width / 1000 / 2; // The frame buffer is just one screen so both eyes share the horizontal resolution
	}
}

void OpenGL_Widget::ApplyCenterToIntrinsics() {
	QPointF centerL, centerR;
	centerL.setX(d_width / 4);
	centerL.setY(d_height / 2);

	// Find the mirror of the left-eye's center of projection
	// around the screen center to find the right eye's COP.
	centerR = QPoint(d_width - centerL.x(), centerL.y());

	Intrinsics[0][0][2] = ( d_cop_l.x() - centerL.x() ) / centerL.x();
	Intrinsics[0][1][2] = ( d_cop_l.y() - centerL.y() ) / centerL.y();

	Intrinsics[1][0][2] = ( d_cop_r.x() - centerR.x() ) / centerR.x();
	Intrinsics[1][1][2] = ( d_cop_r.y() - centerR.y() ) / centerR.y();

	d_cop_l_Prev = d_cop_l;
	d_cop_r_Prev = d_cop_r;
}

void OpenGL_Widget::ApplyIntrincstsToCenter() {
	double CxL, CxR, Cy;
	CxL = d_width / 4;
	CxR = d_width / 2 + CxL;
	Cy = d_height / 2;

	d_cop_l.setX(CxL + (CxL * Intrinsics[0][0][2]));
	d_cop_l.setY(Cy + (Cy * Intrinsics[0][1][2]));

	d_cop_r.setX(CxR + (CxR * Intrinsics[1][0][2]));
	d_cop_r.setY(Cy + (Cy * Intrinsics[1][1][2]));

	d_cop_l_Prev = d_cop_l;
	d_cop_r_Prev = d_cop_r;



}

void OpenGL_Widget::loadInitalValues() {
	if (!loadConfigFromJson(CONFIG_FILE)) {
		printf("ERROR: Unable to load default config file called \"HMD_Config.json\"");
		printf("\n\nPlease ensure this file exists in the same folder as this appication and try again.\n\n");

		// Hack... I don't know how to quit the application and keep the terminal window open so they can see the error message
		// So I just call the pause command on Windows so the user has to manually hit a key before it completely closes.
		#ifdef _WIN32
				system("pause");
		#endif
	
		QApplication::quit();
	}
}

void OpenGL_Widget::resetCenter() {
	double CxL, CxR, Cy;
	CxL = d_width / 4;
	CxR = d_width / 2 + CxL;
	Cy = d_height / 2;

	if ((status & LEFT_EYE) == LEFT_EYE) {
		d_cop_l.setX(CxL);
		d_cop_l.setY(Cy);
		d_cop_l_Prev = d_cop_l;
	}

	if ((status & RIGHT_EYE) == RIGHT_EYE) {
		d_cop_r.setX(CxR);
		d_cop_r.setY(Cy);
		d_cop_r_Prev = d_cop_r;
	}
}

void OpenGL_Widget::resetCoeffiecents() {
	if ((status & LEFT_EYE) && (status & GREEN) && (status & FIRST_COEFFICIENT))
		NLT_Coeffecients[0][0][0] = 0;
	if ((status & LEFT_EYE) && (status & GREEN) && (status & SECOND_COEFFICIENT))
		NLT_Coeffecients[0][0][1] = 0;
	if ((status & LEFT_EYE) && (status & GREEN) && (status & THIRD_COEFFICIENT))
		NLT_Coeffecients[0][0][2] = 0;

	if ((status & LEFT_EYE) && (status & BLUE) && (status & FIRST_COEFFICIENT))
		NLT_Coeffecients[0][1][0] = 0;
	if ((status & LEFT_EYE) && (status & BLUE) && (status & SECOND_COEFFICIENT))
		NLT_Coeffecients[0][1][1] = 0;
	if ((status & LEFT_EYE) && (status & BLUE) && (status & THIRD_COEFFICIENT))
		NLT_Coeffecients[0][1][2] = 0;

	if ((status & LEFT_EYE) && (status & RED) && (status & FIRST_COEFFICIENT))
		NLT_Coeffecients[0][2][0] = 0;
	if ((status & LEFT_EYE) && (status & RED) && (status & SECOND_COEFFICIENT))
		NLT_Coeffecients[0][2][1] = 0;
	if ((status & LEFT_EYE) && (status & RED) && (status & THIRD_COEFFICIENT))
		NLT_Coeffecients[0][2][2] = 0;



	if ((status & RIGHT_EYE) && (status & GREEN) && (status & FIRST_COEFFICIENT))
		NLT_Coeffecients[1][0][0] = 0;
	if ((status & RIGHT_EYE) && (status & GREEN) && (status & SECOND_COEFFICIENT))
		NLT_Coeffecients[1][0][1] = 0;
	if ((status & RIGHT_EYE) && (status & GREEN) && (status & THIRD_COEFFICIENT))
		NLT_Coeffecients[1][0][2] = 0;

	if ((status & RIGHT_EYE) && (status & BLUE) && (status & FIRST_COEFFICIENT))
		NLT_Coeffecients[1][1][0] = 0;
	if ((status & RIGHT_EYE) && (status & BLUE) && (status & SECOND_COEFFICIENT))
		NLT_Coeffecients[1][1][1] = 0;
	if ((status & RIGHT_EYE) && (status & BLUE) && (status & THIRD_COEFFICIENT))
		NLT_Coeffecients[1][1][2] = 0;

	if ((status & RIGHT_EYE) && (status & RED) && (status & FIRST_COEFFICIENT))
		NLT_Coeffecients[1][2][0] = 0;
	if ((status & RIGHT_EYE) && (status & RED) && (status & SECOND_COEFFICIENT))
		NLT_Coeffecients[1][2][1] = 0;
	if ((status & RIGHT_EYE) && (status & RED) && (status & THIRD_COEFFICIENT))
		NLT_Coeffecients[1][2][2] = 0;

}

void OpenGL_Widget::drawImages() {
	update();
	return;


}
void OpenGL_Widget::drawImagesOverlay() {

}

//void OpenGL_Widget::paintEvent(QPaintEvent *event) {
//	QPainter painter(this);
//	QImage img;
//	if (!img.load("garage.png"))
//		printf("failed to load\n");
//	else
//		printf("loaded image\n");
//
//	QPixmap img2;
//	if (!img2.load("garage.png"))
//		printf("failed to load\n");
//	else
//		printf("loaded image\n");
//
//	QRectF target(0, 0, 700.0, 700.0);
//	QRectF source(0.0, 0.0, 700.0, 700.0);
//	painter.setPen(Qt::green);
//	painter.setPen(QPen(Qt::green, 12, Qt::SolidLine, Qt::SquareCap));
//
//
//	//painter.drawText(50, 50, "Test message");
//	//painter.drawImage(target, img, source);
//	//painter.drawLine(0, 0, 500, 500);
//	painter.drawEllipse(300, 300, 100, 100);
//
//	//painter.drawPixmap(100, 100, img2);
//
//	painter.end();
//	printf("here!!!");
//}