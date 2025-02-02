/***************************************************************************
 *   Copyright (c) 2010 J�rgen Riegel (juergen.riegel@web.de)              *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/


#include "PreCompiled.h"
#ifndef _PreComp_
# include <Inventor/nodes/SoPickStyle.h>
# include <QApplication>
#endif

#include <boost/math/special_functions/fpclassify.hpp>
#include <Base/Console.h>

#include <Gui/Action.h>
#include <Gui/Application.h>
#include <Gui/BitmapFactory.h>
#include <Gui/Document.h>
#include <Gui/Command.h>
#include <Gui/MainWindow.h>
#include <Gui/DlgEditFileIncludeProptertyExternal.h>
#include <Gui/Selection.h>
#include <Gui/SelectionFilter.h>
#include <Mod/Sketcher/App/SketchObject.h>

#include "ViewProviderSketch.h"
#include "DrawSketchHandler.h"

#include <Gui/View3DInventor.h>
#include <Gui/View3DInventorViewer.h>
#include <Gui/SoFCUnifiedSelection.h>

using namespace std;
using namespace SketcherGui;


/* helper functions ======================================================*/

// Return counter-clockwise angle from horizontal out of p1 to p2 in radians.
double GetPointAngle (const Base::Vector2D &p1, const Base::Vector2D &p2)
{
  double dX = p2.fX - p1.fX;
  double dY = p2.fY - p1.fY;
  return dY >= 0 ? atan2(dY, dX) : atan2(dY, dX) + 2*M_PI;
}

/*
Find the centerpoint of a circle drawn through any 3 points:

Given points p1-3, draw 2 lines: S12 and S23 which each connect two points.  From the
midpoint of each line, draw a perpendicular line (S12p/S23p) across the circle.  These
lines will cross at the centerpoint.

Mathematically, line S12 will have a slope of m12 which can be determined.  Therefore,
the slope m12p is -1/m12. Line S12p will have an equation of y = m12p*x + b12p.  b12p can
be solved for using the midpoint of the line.  This can be done for both lines.  Since
both S12p and S23p cross at the centerpoint, solving the two equations together will give
the location of the centerpoint.
*/
Base::Vector2D GetCircleCenter (const Base::Vector2D &p1, const Base::Vector2D &p2, const Base::Vector2D &p3)
{
  double m12p = (p1.fX - p2.fX) / (p2.fY - p1.fY);
  double m23p = (p2.fX - p3.fX) / (p3.fY - p2.fY);
  double x = 1/( 2*(m12p - m23p) ) * ( m12p*(p1.fX + p2.fX) -
                                       m23p*(p2.fX + p3.fX) +
                                       p3.fY - p1.fY );
  double y = m12p * ( x - (p1.fX + p2.fX)/2 ) + (p1.fY + p2.fY)/2;

  return Base::Vector2D(x, y);
}

void ActivateHandler(Gui::Document *doc,DrawSketchHandler *handler)
{
    if (doc) {
        if (doc->getInEdit() && doc->getInEdit()->isDerivedFrom
            (SketcherGui::ViewProviderSketch::getClassTypeId()))
            dynamic_cast<SketcherGui::ViewProviderSketch*>
            (doc->getInEdit())->activateHandler(handler);
    }
}

bool isCreateGeoActive(Gui::Document *doc)
{
    if (doc) {
        // checks if a Sketch Viewprovider is in Edit and is in no special mode
        if (doc->getInEdit() && doc->getInEdit()->isDerivedFrom
            (SketcherGui::ViewProviderSketch::getClassTypeId())) {
            if (dynamic_cast<SketcherGui::ViewProviderSketch*>(doc->getInEdit())->
                getSketchMode() == ViewProviderSketch::STATUS_NONE)
                return true;
        }
    }
    return false;
}

SketcherGui::ViewProviderSketch* getSketchViewprovider(Gui::Document *doc)
{
    if (doc) {
        if (doc->getInEdit() && doc->getInEdit()->isDerivedFrom
            (SketcherGui::ViewProviderSketch::getClassTypeId()) )
            return dynamic_cast<SketcherGui::ViewProviderSketch*>(doc->getInEdit());
    }
    return 0;
}


/* Sketch commands =======================================================*/

/* XPM */
static const char *cursor_createline[]={
"32 32 3 1",
"+ c white",
"# c red",
". c None",
"......+.........................",
"......+.........................",
"......+.........................",
"......+.........................",
"......+.........................",
"................................",
"+++++...+++++...................",
"................................",
"......+...............###.......",
"......+...............#.#.......",
"......+...............###.......",
"......+..............#..........",
"......+.............#...........",
"....................#...........",
"...................#............",
"..................#.............",
"..................#.............",
".................#..............",
"................#...............",
"................#...............",
"...............#................",
"..............#.................",
"..............#.................",
".............#..................",
"..........###...................",
"..........#.#...................",
"..........###...................",
"................................",
"................................",
"................................",
"................................",
"................................"};

class DrawSketchHandlerLine: public DrawSketchHandler
{
public:
    DrawSketchHandlerLine():Mode(STATUS_SEEK_First),EditCurve(2){}
    virtual ~DrawSketchHandlerLine(){}
    /// mode table
    enum SelectMode {
        STATUS_SEEK_First,      /**< enum value ----. */
        STATUS_SEEK_Second,     /**< enum value ----. */
        STATUS_End
    };

    virtual void activated(ViewProviderSketch *sketchgui)
    {
        setCursor(QPixmap(cursor_createline),7,7);
    }

    virtual void mouseMove(Base::Vector2D onSketchPos)
    {
        if (Mode==STATUS_SEEK_First) {
            setPositionText(onSketchPos);
            if (seekAutoConstraint(sugConstr1, onSketchPos, Base::Vector2D(0.f,0.f))) {
                renderSuggestConstraintsCursor(sugConstr1);
                return;
            }
        }
        else if (Mode==STATUS_SEEK_Second){
            float length = (onSketchPos - EditCurve[0]).Length();
            float angle = (onSketchPos - EditCurve[0]).GetAngle(Base::Vector2D(1.f,0.f));
            SbString text;
            text.sprintf(" (%.1f,%.1fdeg)", length, angle * 180 / M_PI);
            setPositionText(onSketchPos, text);

            EditCurve[1] = onSketchPos;
            sketchgui->drawEdit(EditCurve);
            if (seekAutoConstraint(sugConstr2, onSketchPos, onSketchPos - EditCurve[0])) {
                renderSuggestConstraintsCursor(sugConstr2);
                return;
            }
        }
        applyCursor();
    }

    virtual bool pressButton(Base::Vector2D onSketchPos)
    {
        if (Mode==STATUS_SEEK_First){
            EditCurve[0] = onSketchPos;
            Mode = STATUS_SEEK_Second;
        }
        else {
            EditCurve[1] = onSketchPos;
            sketchgui->drawEdit(EditCurve);
            Mode = STATUS_End;
        }
        return true;
    }

    virtual bool releaseButton(Base::Vector2D onSketchPos)
    {
        if (Mode==STATUS_End){
            unsetCursor();
            resetPositionText();

            Gui::Command::openCommand("Add sketch line");
            Gui::Command::doCommand(Gui::Command::Doc,"App.ActiveDocument.%s.addGeometry(Part.Line(App.Vector(%f,%f,0),App.Vector(%f,%f,0)))",
                      sketchgui->getObject()->getNameInDocument(),
                      EditCurve[0].fX,EditCurve[0].fY,EditCurve[1].fX,EditCurve[1].fY);
            Gui::Command::commitCommand();
            Gui::Command::updateActive();

            // add auto constraints for the line segment start
            if (sugConstr1.size() > 0) {
                createAutoConstraints(sugConstr1, getHighestCurveIndex(), Sketcher::start);
                sugConstr1.clear();
            }

            // add auto constraints for the line segment end
            if (sugConstr2.size() > 0) {
                createAutoConstraints(sugConstr2, getHighestCurveIndex(), Sketcher::end);
                sugConstr2.clear();
            }

            EditCurve.clear();
            sketchgui->drawEdit(EditCurve);
            sketchgui->purgeHandler(); // no code after this line, Handler get deleted in ViewProvider
        }
        return true;
    }
protected:
    SelectMode Mode;
    std::vector<Base::Vector2D> EditCurve;
    std::vector<AutoConstraint> sugConstr1, sugConstr2;
};

DEF_STD_CMD_A(CmdSketcherCreateLine);

CmdSketcherCreateLine::CmdSketcherCreateLine()
  : Command("Sketcher_CreateLine")
{
    sAppModule      = "Sketcher";
    sGroup          = QT_TR_NOOP("Sketcher");
    sMenuText       = QT_TR_NOOP("Create line");
    sToolTipText    = QT_TR_NOOP("Create a line in the sketch");
    sWhatsThis      = sToolTipText;
    sStatusTip      = sToolTipText;
    sPixmap         = "Sketcher_CreateLine";
    sAccel          = "L";
    eType           = ForEdit;
}

void CmdSketcherCreateLine::activated(int iMsg)
{
    ActivateHandler(getActiveGuiDocument(),new DrawSketchHandlerLine() );
}

bool CmdSketcherCreateLine::isActive(void)
{
    return isCreateGeoActive(getActiveGuiDocument());
}


/* Create Box =======================================================*/

/* XPM */
static const char *cursor_createbox[]={
"32 32 3 1",
"+ c white",
"# c red",
". c None",
"......+.........................",
"......+.........................",
"......+.........................",
"......+.........................",
"......+.........................",
"................................",
"+++++...+++++...................",
"................................",
"......+.........................",
"......+.........................",
"......+.........................",
"......+.........................",
"......+.........................",
"................................",
"................................",
"..........................###...",
"...........################.#...",
"...........#..............###...",
"...........#...............#....",
"...........#...............#....",
"...........#...............#....",
"...........#...............#....",
"...........#...............#....",
"...........#...............#....",
"..........###..............#....",
"..........#.################....",
"..........###...................",
"................................",
"................................",
"................................",
"................................",
"................................"};

class DrawSketchHandlerBox: public DrawSketchHandler
{
public:
    DrawSketchHandlerBox():Mode(STATUS_SEEK_First),EditCurve(5){}
    virtual ~DrawSketchHandlerBox(){}
    /// mode table
    enum BoxMode {
        STATUS_SEEK_First,      /**< enum value ----. */
        STATUS_SEEK_Second,     /**< enum value ----. */
        STATUS_End
    };

    virtual void activated(ViewProviderSketch *sketchgui)
    {
        setCursor(QPixmap(cursor_createbox),7,7);
    }

    virtual void mouseMove(Base::Vector2D onSketchPos)
    {

        if (Mode==STATUS_SEEK_First) {
            setPositionText(onSketchPos);
            if (seekAutoConstraint(sugConstr1, onSketchPos, Base::Vector2D(0.f,0.f))) {
                renderSuggestConstraintsCursor(sugConstr1);
                return;
            }
        }
        else if (Mode==STATUS_SEEK_Second) {
            float dx = onSketchPos.fX - EditCurve[0].fX;
            float dy = onSketchPos.fY - EditCurve[0].fY;
            SbString text;
            text.sprintf(" (%.1f x %.1f)", dx, dy);
            setPositionText(onSketchPos, text);

            EditCurve[2] = onSketchPos;
            EditCurve[1] = Base::Vector2D(onSketchPos.fX ,EditCurve[0].fY);
            EditCurve[3] = Base::Vector2D(EditCurve[0].fX,onSketchPos.fY);
            sketchgui->drawEdit(EditCurve);
            if (seekAutoConstraint(sugConstr2, onSketchPos, Base::Vector2D(0.0,0.0))) {
                renderSuggestConstraintsCursor(sugConstr2);
                return;
            }
        }
        applyCursor();
    }

    virtual bool pressButton(Base::Vector2D onSketchPos)
    {
        if (Mode==STATUS_SEEK_First){
            EditCurve[0] = onSketchPos;
            EditCurve[4] = onSketchPos;
            Mode = STATUS_SEEK_Second;
        }
        else {
            EditCurve[2] = onSketchPos;
            EditCurve[1] = Base::Vector2D(onSketchPos.fX ,EditCurve[0].fY);
            EditCurve[3] = Base::Vector2D(EditCurve[0].fX,onSketchPos.fY);
            sketchgui->drawEdit(EditCurve);
            Mode = STATUS_End;
        }
        return true;
    }

    virtual bool releaseButton(Base::Vector2D onSketchPos)
    {
        if (Mode==STATUS_End){
            unsetCursor();
            resetPositionText();
            Gui::Command::openCommand("Add sketch box");
            int firstCurve = getHighestCurveIndex() + 1;
            // add the four line geos
            Gui::Command::doCommand(Gui::Command::Doc,"App.ActiveDocument.%s.addGeometry(Part.Line(App.Vector(%f,%f,0),App.Vector(%f,%f,0)))",
                      sketchgui->getObject()->getNameInDocument(),
                      EditCurve[0].fX,EditCurve[0].fY,EditCurve[1].fX,EditCurve[1].fY);
            Gui::Command::doCommand(Gui::Command::Doc,"App.ActiveDocument.%s.addGeometry(Part.Line(App.Vector(%f,%f,0),App.Vector(%f,%f,0)))",
                      sketchgui->getObject()->getNameInDocument(),
                      EditCurve[1].fX,EditCurve[1].fY,EditCurve[2].fX,EditCurve[2].fY);
            Gui::Command::doCommand(Gui::Command::Doc,"App.ActiveDocument.%s.addGeometry(Part.Line(App.Vector(%f,%f,0),App.Vector(%f,%f,0)))",
                      sketchgui->getObject()->getNameInDocument(),
                      EditCurve[2].fX,EditCurve[2].fY,EditCurve[3].fX,EditCurve[3].fY);
            Gui::Command::doCommand(Gui::Command::Doc,"App.ActiveDocument.%s.addGeometry(Part.Line(App.Vector(%f,%f,0),App.Vector(%f,%f,0)))",
                      sketchgui->getObject()->getNameInDocument(),
                      EditCurve[3].fX,EditCurve[3].fY,EditCurve[0].fX,EditCurve[0].fY);
            // add the four coincidents to ty them together
            Gui::Command::doCommand(Gui::Command::Doc,"App.ActiveDocument.%s.addConstraint(Sketcher.Constraint('Coincident',%i,2,%i,1)) "
                     ,sketchgui->getObject()->getNameInDocument()
                     ,firstCurve,firstCurve+1);
            Gui::Command::doCommand(Gui::Command::Doc,"App.ActiveDocument.%s.addConstraint(Sketcher.Constraint('Coincident',%i,2,%i,1)) "
                     ,sketchgui->getObject()->getNameInDocument()
                     ,firstCurve+1,firstCurve+2);
            Gui::Command::doCommand(Gui::Command::Doc,"App.ActiveDocument.%s.addConstraint(Sketcher.Constraint('Coincident',%i,2,%i,1)) "
                     ,sketchgui->getObject()->getNameInDocument()
                     ,firstCurve+2,firstCurve+3);
            Gui::Command::doCommand(Gui::Command::Doc,"App.ActiveDocument.%s.addConstraint(Sketcher.Constraint('Coincident',%i,2,%i,1)) "
                     ,sketchgui->getObject()->getNameInDocument()
                     ,firstCurve+3,firstCurve);
            // add the horizontal constraints
            Gui::Command::doCommand(Gui::Command::Doc,"App.ActiveDocument.%s.addConstraint(Sketcher.Constraint('Horizontal',%i)) "
                     ,sketchgui->getObject()->getNameInDocument()
                     ,firstCurve);
            Gui::Command::doCommand(Gui::Command::Doc,"App.ActiveDocument.%s.addConstraint(Sketcher.Constraint('Horizontal',%i)) "
                     ,sketchgui->getObject()->getNameInDocument()
                     ,firstCurve+2);
            // add the vertical constraints
            Gui::Command::doCommand(Gui::Command::Doc,"App.ActiveDocument.%s.addConstraint(Sketcher.Constraint('Vertical',%i)) "
                     ,sketchgui->getObject()->getNameInDocument()
                     ,firstCurve+1);
            Gui::Command::doCommand(Gui::Command::Doc,"App.ActiveDocument.%s.addConstraint(Sketcher.Constraint('Vertical',%i)) "
                     ,sketchgui->getObject()->getNameInDocument()
                     ,firstCurve+3);

            Gui::Command::commitCommand();
            Gui::Command::updateActive();

            // add auto constraints at the start of the first side
            if (sugConstr1.size() > 0) {
                createAutoConstraints(sugConstr1, getHighestCurveIndex() - 3 , Sketcher::start);
                sugConstr1.clear();
            }

            // add auto constraints at the end of the second side
            if (sugConstr2.size() > 0) {
                createAutoConstraints(sugConstr2, getHighestCurveIndex() - 2, Sketcher::end);
                sugConstr2.clear();
            }

            EditCurve.clear();
            sketchgui->drawEdit(EditCurve);
            sketchgui->purgeHandler(); // no code after this line, Handler get deleted in ViewProvider
        }
        return true;
    }
protected:
    BoxMode Mode;
    std::vector<Base::Vector2D> EditCurve;
    std::vector<AutoConstraint> sugConstr1, sugConstr2;
};

DEF_STD_CMD_A(CmdSketcherCreateRectangle);

CmdSketcherCreateRectangle::CmdSketcherCreateRectangle()
  : Command("Sketcher_CreateRectangle")
{
    sAppModule      = "Sketcher";
    sGroup          = QT_TR_NOOP("Sketcher");
    sMenuText       = QT_TR_NOOP("Create rectangle");
    sToolTipText    = QT_TR_NOOP("Create a rectangle in the sketch");
    sWhatsThis      = sToolTipText;
    sStatusTip      = sToolTipText;
    sPixmap         = "Sketcher_CreateRectangle";
    sAccel          = "R";
    eType           = ForEdit;
}

void CmdSketcherCreateRectangle::activated(int iMsg)
{
    ActivateHandler(getActiveGuiDocument(),new DrawSketchHandlerBox() );
}

bool CmdSketcherCreateRectangle::isActive(void)
{
    return isCreateGeoActive(getActiveGuiDocument());
}


// ======================================================================================

/* XPM */
static const char *cursor_createlineset[]={
"32 32 3 1",
"+ c white",
"# c red",
". c None",
"......+.........................",
"......+.........................",
"......+.........................",
"......+.........................",
"......+.........................",
"................................",
"+++++...+++++...................",
"................................",
"......+...............###.......",
"......+...............#.#.......",
"......+...............###.......",
"......+..............#..#.......",
"......+.............#....#......",
"....................#....#......",
"...................#......#.....",
"..................#.......#.....",
"..................#........#....",
".................#.........#....",
"................#..........###..",
"................#..........#.#..",
"......#........#...........###..",
".......#......#.................",
"........#.....#.................",
".........#...#..................",
"..........###...................",
"..........#.#...................",
"..........###...................",
"................................",
"................................",
"................................",
"................................",
"................................"};

class DrawSketchHandlerLineSet: public DrawSketchHandler
{
public:
    DrawSketchHandlerLineSet()
      : Mode(STATUS_SEEK_First),SegmentMode(SEGMENT_MODE_Line),
        TransitionMode(TRANSITION_MODE_Free),suppressTransition(false),EditCurve(2),
        firstCurve(-1),previousCurve(-1),
        firstPosId(Sketcher::none),previousPosId(Sketcher::none) {}
    virtual ~DrawSketchHandlerLineSet() {}
    /// mode table
    enum SELECT_MODE {
        STATUS_SEEK_First,      /**< enum value ----. */
        STATUS_SEEK_Second,     /**< enum value ----. */
        STATUS_Do,
        STATUS_Close
    };

    enum SEGMENT_MODE
    {
        SEGMENT_MODE_Arc,
        SEGMENT_MODE_Line
    };

    enum TRANSITION_MODE
    {
        TRANSITION_MODE_Free,
        TRANSITION_MODE_Tangent,
        TRANSITION_MODE_Perpendicular_L,
        TRANSITION_MODE_Perpendicular_R
    };

    virtual void registerPressedKey(bool pressed, int key)
    {
        if (Mode != STATUS_SEEK_Second)
            return; // SegmentMode can be changed only in STATUS_SEEK_Second mode

        if (key == SoKeyboardEvent::M && pressed && previousCurve != -1) {
            // loop through the following modes:
            // SEGMENT_MODE_Line, TRANSITION_MODE_Free / TRANSITION_MODE_Tangent
            // SEGMENT_MODE_Line, TRANSITION_MODE_Perpendicular_L
            // SEGMENT_MODE_Line, TRANSITION_MODE_Tangent / TRANSITION_MODE_Free
            // SEGMENT_MODE_Arc, TRANSITION_MODE_Tangent
            // SEGMENT_MODE_Arc, TRANSITION_MODE_Perpendicular_L
            // SEGMENT_MODE_Arc, TRANSITION_MODE_Perpendicular_R

            Base::Vector2D onSketchPos;
            if (SegmentMode == SEGMENT_MODE_Line)
                onSketchPos = EditCurve[EditCurve.size()-1];
            else
                onSketchPos = EditCurve[29];

            const Part::Geometry *geom = sketchgui->getSketchObject()->getGeometry(previousCurve);

            if (SegmentMode == SEGMENT_MODE_Line) {
                switch (TransitionMode) {
                    case TRANSITION_MODE_Free:
                        if (geom->getTypeId() == Part::GeomArcOfCircle::getClassTypeId()) { // 3rd mode
                            SegmentMode = SEGMENT_MODE_Arc;
                            TransitionMode = TRANSITION_MODE_Tangent;
                        }
                        else // 1st mode
                            TransitionMode = TRANSITION_MODE_Perpendicular_L;
                        break;
                    case TRANSITION_MODE_Perpendicular_L: // 2nd mode
                        if (geom->getTypeId() == Part::GeomArcOfCircle::getClassTypeId())
                            TransitionMode = TRANSITION_MODE_Free;
                        else
                            TransitionMode = TRANSITION_MODE_Tangent;
                        break;
                    case TRANSITION_MODE_Tangent:
                        if (geom->getTypeId() == Part::GeomArcOfCircle::getClassTypeId()) // 1st mode
                            TransitionMode = TRANSITION_MODE_Perpendicular_L;
                        else { // 3rd mode
                            SegmentMode = SEGMENT_MODE_Arc;
                            TransitionMode = TRANSITION_MODE_Tangent;
                        }
                        break;
                    default: // unexpected mode
                        TransitionMode = TRANSITION_MODE_Free;
                        break;
                }
            }
            else {
                switch (TransitionMode) {
                    case TRANSITION_MODE_Tangent: // 4th mode
                        TransitionMode = TRANSITION_MODE_Perpendicular_L;
                        break;
                    case TRANSITION_MODE_Perpendicular_L: // 5th mode
                        TransitionMode = TRANSITION_MODE_Perpendicular_R;
                        break;
                    default: // 6th mode (Perpendicular_R) + unexpexted mode
                        SegmentMode = SEGMENT_MODE_Line;
                        if (geom->getTypeId() == Part::GeomArcOfCircle::getClassTypeId())
                            TransitionMode = TRANSITION_MODE_Tangent;
                        else
                            TransitionMode = TRANSITION_MODE_Free;
                        break;
                }
            }

            if (SegmentMode == SEGMENT_MODE_Line)
                EditCurve.resize(TransitionMode == TRANSITION_MODE_Free ? 2 : 3);
            else
                EditCurve.resize(32);
            mouseMove(onSketchPos); // trigger an update of EditCurve
        }
    }

    virtual void activated(ViewProviderSketch *sketchgui)
    {
        setCursor(QPixmap(cursor_createlineset),7,7);
    }

    virtual void mouseMove(Base::Vector2D onSketchPos)
    {
        suppressTransition = false;
        if (Mode==STATUS_SEEK_First) {
            setPositionText(onSketchPos);
            if (seekAutoConstraint(sugConstr1, onSketchPos, Base::Vector2D(0.f,0.f))) {
                renderSuggestConstraintsCursor(sugConstr1);
                return;
            }
        }
        else if (Mode==STATUS_SEEK_Second){
            if (SegmentMode == SEGMENT_MODE_Line) {
                EditCurve[EditCurve.size()-1] = onSketchPos;
                if (TransitionMode == TRANSITION_MODE_Tangent) {
                    Base::Vector2D Tangent(dirVec.x,dirVec.y);
                    EditCurve[1].ProjToLine(EditCurve[2] - EditCurve[0], Tangent);
                    if (EditCurve[1] * Tangent < 0) {
                        EditCurve[1] = EditCurve[2];
                        suppressTransition = true;
                    }
                    else
                        EditCurve[1] = EditCurve[0] + EditCurve[1];
                }
                else if (TransitionMode == TRANSITION_MODE_Perpendicular_L ||
                         TransitionMode == TRANSITION_MODE_Perpendicular_R) {
                    Base::Vector2D Perpendicular(-dirVec.y,dirVec.x);
                    EditCurve[1].ProjToLine(EditCurve[2] - EditCurve[0], Perpendicular);
                    EditCurve[1] = EditCurve[0] + EditCurve[1];
                }

                sketchgui->drawEdit(EditCurve);

                float length = (EditCurve[1] - EditCurve[0]).Length();
                float angle = (EditCurve[1] - EditCurve[0]).GetAngle(Base::Vector2D(1.f,0.f));

                SbString text;
                text.sprintf(" (%.1f,%.1fdeg)", length, angle * 180 / M_PI);
                setPositionText(EditCurve[1], text);

                if (TransitionMode == TRANSITION_MODE_Free) {
                    if (seekAutoConstraint(sugConstr2, onSketchPos, onSketchPos - EditCurve[0])) {
                        renderSuggestConstraintsCursor(sugConstr2);
                        return;
                    }
                }
            }
            else if (SegmentMode == SEGMENT_MODE_Arc) {
                Base::Vector2D Tangent;
                if  (TransitionMode == TRANSITION_MODE_Tangent)
                    Tangent = Base::Vector2D(dirVec.x,dirVec.y);
                else if  (TransitionMode == TRANSITION_MODE_Perpendicular_L)
                    Tangent = Base::Vector2D(-dirVec.y,dirVec.x);
                else if  (TransitionMode == TRANSITION_MODE_Perpendicular_R)
                    Tangent = Base::Vector2D(dirVec.y,-dirVec.x);

                double theta = Tangent.GetAngle(onSketchPos - EditCurve[0]);
                arcRadius = (onSketchPos - EditCurve[0]).Length()/(2.0*sin(theta));
                // At this point we need a unit normal vector pointing torwards
                // the center of the arc we are drawing. Derivation of the formula
                // used here can be found at http://people.richland.edu/james/lecture/m116/matrices/area.html
                double x1 = EditCurve[0].fX;
                double y1 = EditCurve[0].fY;
                double x2 = x1 + Tangent.fX;
                double y2 = y1 + Tangent.fY;
                double x3 = onSketchPos.fX;
                double y3 = onSketchPos.fY;
                if ((x2*y3-x3*y2)-(x1*y3-x3*y1)+(x1*y2-x2*y1) > 0)
                    arcRadius *= -1;
                if (boost::math::isnan(arcRadius) || boost::math::isinf(arcRadius))
                    arcRadius = 0.f;

                CenterPoint = EditCurve[0] + Base::Vector2D(arcRadius * Tangent.fY, -arcRadius * Tangent.fX);

                double rx = EditCurve[0].fX - CenterPoint.fX;
                double ry = EditCurve[0].fY - CenterPoint.fY;

                startAngle = atan2(ry,rx);

                double rxe = onSketchPos.fX - CenterPoint.fX;
                double rye = onSketchPos.fY - CenterPoint.fY;
                double arcAngle = atan2(-rxe*ry + rye*rx, rxe*rx + rye*ry);
                if (boost::math::isnan(arcAngle) || boost::math::isinf(arcAngle))
                    arcAngle = 0.f;
                if (arcRadius >= 0 && arcAngle > 0)
                    arcAngle -=  2*M_PI;
                if (arcRadius < 0 && arcAngle < 0)
                    arcAngle +=  2*M_PI;
                endAngle = startAngle + arcAngle;

                for (int i=1; i <= 29; i++) {
                    double angle = i*arcAngle/29.0;
                    double dx = rx * cos(angle) - ry * sin(angle);
                    double dy = rx * sin(angle) + ry * cos(angle);
                    EditCurve[i] = Base::Vector2D(CenterPoint.fX + dx, CenterPoint.fY + dy);
                }

                EditCurve[30] = CenterPoint;
                EditCurve[31] = EditCurve[0];

                sketchgui->drawEdit(EditCurve);

                SbString text;
                text.sprintf(" (%.1fR,%.1fdeg)", std::abs(arcRadius), arcAngle * 180 / M_PI);
                setPositionText(onSketchPos, text);

                if (seekAutoConstraint(sugConstr2, onSketchPos, Base::Vector2D(0.f,0.f))) {
                    renderSuggestConstraintsCursor(sugConstr2);
                    return;
                }
            }
        }
        applyCursor();
    }

    virtual bool pressButton(Base::Vector2D onSketchPos)
    {
        if (Mode == STATUS_SEEK_First) {

            EditCurve[0] = onSketchPos; // this may be overwritten if previousCurve is found

            // here we check if there is a preselected point and
            // we set up a transition from the neighbouring segment.
            // (peviousCurve, previousPosId, dirVec, TransitionMode)
            for (unsigned int i=0; i < sugConstr1.size(); i++)
                if (sugConstr1[i].Type == Sketcher::Coincident) {
                    const Part::Geometry *geom = sketchgui->getSketchObject()->getGeometry(sugConstr1[i].GeoId);
                    if ((geom->getTypeId() == Part::GeomLineSegment::getClassTypeId() ||
                         geom->getTypeId() == Part::GeomArcOfCircle::getClassTypeId()) &&
                        (sugConstr1[i].PosId == Sketcher::start ||
                         sugConstr1[i].PosId == Sketcher::end)) {
                        previousCurve = sugConstr1[i].GeoId;
                        previousPosId = sugConstr1[i].PosId;
                        updateTransitionData(previousCurve,previousPosId); // -> dirVec, EditCurve[0]
                        if (geom->getTypeId() == Part::GeomArcOfCircle::getClassTypeId())
                            TransitionMode = TRANSITION_MODE_Tangent;
                        sugConstr1.erase(sugConstr1.begin()+i); // actually we should clear the vector completely
                        break;
                    }
                }

            // remember our first point (even if we are doing a transition from a previous curve)
            firstCurve = getHighestCurveIndex() + 1;
            firstPosId = Sketcher::start;

            if (SegmentMode == SEGMENT_MODE_Line)
                EditCurve.resize(TransitionMode == TRANSITION_MODE_Free ? 2 : 3);
            else if (SegmentMode == SEGMENT_MODE_Arc)
                EditCurve.resize(32);
            Mode = STATUS_SEEK_Second;
        }
        else if (Mode == STATUS_SEEK_Second) {
            // exit on clicking exactly at the same position (e.g. double click)
            if (onSketchPos == EditCurve[0]) {
                unsetCursor();
                EditCurve.clear();
                resetPositionText();
                sketchgui->drawEdit(EditCurve);
                sketchgui->purgeHandler(); // no code after this line, Handler get deleted in ViewProvider
            }

            Mode = STATUS_Do;
            if (sketchgui->getPreselectPoint() != -1 && firstPosId != Sketcher::none) {
                int GeoId;
                Sketcher::PointPos PosId;
                sketchgui->getSketchObject()->getGeoVertexIndex(sketchgui->getPreselectPoint(),GeoId,PosId);
                if (sketchgui->getSketchObject()->arePointsCoincident(GeoId,PosId,firstCurve,firstPosId))
                    Mode = STATUS_Close;
            }
            else if (sketchgui->getPreselectCross() == 0 && firstPosId != Sketcher::none) {
                // close line started at root point
                if (sketchgui->getSketchObject()->arePointsCoincident(-1,Sketcher::start,firstCurve,firstPosId))
                    Mode = STATUS_Close;
            }
        }
        return true;
    }

    virtual bool releaseButton(Base::Vector2D onSketchPos)
    {
        if (Mode == STATUS_Do || Mode == STATUS_Close) {

            if (SegmentMode == SEGMENT_MODE_Line) {
                // open the transaction
                Gui::Command::openCommand("Add line to sketch wire");
                // issue the geometry
                Gui::Command::doCommand(Gui::Command::Doc,
                    "App.ActiveDocument.%s.addGeometry(Part.Line(App.Vector(%f,%f,0),App.Vector(%f,%f,0)))",
                    sketchgui->getObject()->getNameInDocument(),
                    EditCurve[0].fX,EditCurve[0].fY,EditCurve[1].fX,EditCurve[1].fY);
            }
            else if (SegmentMode == SEGMENT_MODE_Arc) { // We're dealing with an Arc
                if (!boost::math::isnormal(arcRadius)) {
                    Mode = STATUS_SEEK_Second;
                    return true;
                }
                Gui::Command::openCommand("Add arc to sketch wire");
                Gui::Command::doCommand(Gui::Command::Doc,
                    "App.ActiveDocument.%s.addGeometry(Part.ArcOfCircle"
                    "(Part.Circle(App.Vector(%f,%f,0),App.Vector(0,0,1),%f),%f,%f))",
                    sketchgui->getObject()->getNameInDocument(),
                    CenterPoint.fX, CenterPoint.fY, std::abs(arcRadius),
                    std::min(startAngle,endAngle), std::max(startAngle,endAngle));
            }
            // issue the constraint
            if (previousPosId != Sketcher::none) {
                int lastCurve = getHighestCurveIndex();
                Sketcher::PointPos lastStartPosId = (SegmentMode == SEGMENT_MODE_Arc && startAngle > endAngle) ?
                                                    Sketcher::end : Sketcher::start;
                Sketcher::PointPos lastEndPosId = (SegmentMode == SEGMENT_MODE_Arc && startAngle > endAngle) ?
                                                  Sketcher::start : Sketcher::end;
                // in case of a tangency constraint, the coincident constraint is redundant
                std::string constrType = "Coincident";
                if (!suppressTransition && previousCurve != -1) {
                    if (TransitionMode == TRANSITION_MODE_Tangent)
                        constrType = "Tangent";
                    else if (TransitionMode == TRANSITION_MODE_Perpendicular_L ||
                             TransitionMode == TRANSITION_MODE_Perpendicular_R)
                        constrType = "Perpendicular";
                }
                Gui::Command::doCommand(Gui::Command::Doc,
                    "App.ActiveDocument.%s.addConstraint(Sketcher.Constraint('%s',%i,%i,%i,%i)) ",
                    sketchgui->getObject()->getNameInDocument(), constrType.c_str(),
                    previousCurve, previousPosId, lastCurve, lastStartPosId);
                if (Mode == STATUS_Close) {
                    // close the loop by constrain to the first curve point
                    Gui::Command::doCommand(Gui::Command::Doc,
                        "App.ActiveDocument.%s.addConstraint(Sketcher.Constraint('Coincident',%i,%i,%i,%i)) ",
                        sketchgui->getObject()->getNameInDocument(),
                        lastCurve,lastEndPosId,firstCurve,firstPosId);
                }
                Gui::Command::commitCommand();
                Gui::Command::updateActive();
            }

            if (Mode == STATUS_Close) {
                if (sugConstr2.size() > 0) {
                    // exclude any coincidence constraints
                    std::vector<AutoConstraint> sugConstr;
                    for (unsigned int i=0; i < sugConstr2.size(); i++) {
                        if (sugConstr2[i].Type != Sketcher::Coincident)
                            sugConstr.push_back(sugConstr2[i]);
                    }
                    createAutoConstraints(sugConstr, getHighestCurveIndex(), Sketcher::end);
                    sugConstr2.clear();
                }

                unsetCursor();
                EditCurve.clear();
                resetPositionText();
                sketchgui->drawEdit(EditCurve);
                sketchgui->purgeHandler(); // no code after this line, Handler get deleted in ViewProvider
            }
            else {
                Gui::Command::commitCommand();
                Gui::Command::updateActive();

                // Add auto constraints
                if (sugConstr1.size() > 0) { // this is relevant only to the very first point
                    createAutoConstraints(sugConstr1, getHighestCurveIndex(), Sketcher::start);
                    sugConstr1.clear();
                }

                if (sugConstr2.size() > 0) {
                    createAutoConstraints(sugConstr2, getHighestCurveIndex(), Sketcher::end);
                    sugConstr2.clear();
                }

                // remember the vertex for the next rounds constraint..
                previousCurve = getHighestCurveIndex();
                previousPosId = (SegmentMode == SEGMENT_MODE_Arc && startAngle > endAngle) ?
                                 Sketcher::start : Sketcher::end; // cw arcs are rendered in reverse

                // setup for the next line segment
                // calculate dirVec and EditCurve[0]
                updateTransitionData(previousCurve,previousPosId);

                applyCursor();
                Mode = STATUS_SEEK_Second;
                if (SegmentMode == SEGMENT_MODE_Arc) {
                    TransitionMode = TRANSITION_MODE_Tangent;
                    EditCurve.resize(3);
                    EditCurve[2] = EditCurve[0];
                }
                else {
                    TransitionMode = TRANSITION_MODE_Free;
                    EditCurve.resize(2);
                }
                SegmentMode = SEGMENT_MODE_Line;
                EditCurve[1] = EditCurve[0];
                mouseMove(onSketchPos); // trigger an update of EditCurve
            }
        }
        return true;
    }
protected:
    SELECT_MODE Mode;
    SEGMENT_MODE SegmentMode;
    TRANSITION_MODE TransitionMode;
    bool suppressTransition;

    std::vector<Base::Vector2D> EditCurve;
    int firstCurve;
    int previousCurve;
    Sketcher::PointPos firstPosId;
    Sketcher::PointPos previousPosId;
    std::vector<AutoConstraint> sugConstr1, sugConstr2;

    Base::Vector2D CenterPoint;
    Base::Vector3d dirVec;
    double startAngle, endAngle, arcRadius;

    void updateTransitionData(int GeoId, Sketcher::PointPos PosId) {

        // Use updated startPoint/endPoint as autoconstraints can modify the position
        const Part::Geometry *geom = sketchgui->getSketchObject()->getGeometry(GeoId);
        if (geom->getTypeId() == Part::GeomLineSegment::getClassTypeId()) {
            const Part::GeomLineSegment *lineSeg = dynamic_cast<const Part::GeomLineSegment *>(geom);
            dirVec.Set(lineSeg->getEndPoint().x - lineSeg->getStartPoint().x,
                       lineSeg->getEndPoint().y - lineSeg->getStartPoint().y,
                       0.f);
            if (PosId == Sketcher::start) {
                dirVec *= -1;
                EditCurve[0] = Base::Vector2D(lineSeg->getStartPoint().x, lineSeg->getStartPoint().y);
            }
            else
                EditCurve[0] = Base::Vector2D(lineSeg->getEndPoint().x, lineSeg->getEndPoint().y);
        }
        else if (geom->getTypeId() == Part::GeomArcOfCircle::getClassTypeId()) {
            const Part::GeomArcOfCircle *arcSeg = dynamic_cast<const Part::GeomArcOfCircle *>(geom);
            if (PosId == Sketcher::start) {
                EditCurve[0] = Base::Vector2D(arcSeg->getStartPoint().x,arcSeg->getStartPoint().y);
                dirVec = Base::Vector3d(0.f,0.f,-1.0) % (arcSeg->getStartPoint()-arcSeg->getCenter());
            }
            else {
                EditCurve[0] = Base::Vector2D(arcSeg->getEndPoint().x,arcSeg->getEndPoint().y);
                dirVec = Base::Vector3d(0.f,0.f,1.0) % (arcSeg->getEndPoint()-arcSeg->getCenter());
            }
        }
        dirVec.Normalize();
    }
};

DEF_STD_CMD_A(CmdSketcherCreatePolyline);

CmdSketcherCreatePolyline::CmdSketcherCreatePolyline()
  : Command("Sketcher_CreatePolyline")
{
    sAppModule      = "Sketcher";
    sGroup          = QT_TR_NOOP("Sketcher");
    sMenuText       = QT_TR_NOOP("Create polyline");
    sToolTipText    = QT_TR_NOOP("Create a polyline in the sketch. 'M' Key cycles behaviour");
    sWhatsThis      = sToolTipText;
    sStatusTip      = sToolTipText;
    sPixmap         = "Sketcher_CreatePolyline";
    eType           = ForEdit;
}

void CmdSketcherCreatePolyline::activated(int iMsg)
{
    ActivateHandler(getActiveGuiDocument(),new DrawSketchHandlerLineSet() );
}

bool CmdSketcherCreatePolyline::isActive(void)
{
    return isCreateGeoActive(getActiveGuiDocument());
}


// ======================================================================================

/* XPM */
static const char *cursor_createarc[]={
"32 32 3 1",
"+ c white",
"# c red",
". c None",
"......+...........###...........",
"......+...........#.#...........",
"......+...........###...........",
"......+..............##.........",
"......+...............##........",
".......................#........",
"+++++...+++++...........#.......",
"........................##......",
"......+..................#......",
"......+..................#......",
"......+...................#.....",
"......+...................#.....",
"......+...................#.....",
"..........................#.....",
"..........................#.....",
"..........................#.....",
"..........................#.....",
".........................#......",
".........................#......",
"........................#.......",
"........................#.......",
"...###.................#........",
"...#.#................#.........",
"...###...............#..........",
"......##...........##...........",
".......###.......##.............",
"..........#######...............",
"................................",
"................................",
"................................",
"................................",
"................................"};

class DrawSketchHandlerArc : public DrawSketchHandler
{
public:
    DrawSketchHandlerArc()
      : Mode(STATUS_SEEK_First),EditCurve(2){}
    virtual ~DrawSketchHandlerArc(){}
    /// mode table
    enum SelectMode {
        STATUS_SEEK_First,      /**< enum value ----. */
        STATUS_SEEK_Second,     /**< enum value ----. */
        STATUS_SEEK_Third,      /**< enum value ----. */
        STATUS_End
    };

    virtual void activated(ViewProviderSketch *sketchgui)
    {
        setCursor(QPixmap(cursor_createarc),7,7);
    }

    virtual void mouseMove(Base::Vector2D onSketchPos)
    {
        if (Mode==STATUS_SEEK_First) {
            setPositionText(onSketchPos);
            if (seekAutoConstraint(sugConstr1, onSketchPos, Base::Vector2D(0.f,0.f))) {
                renderSuggestConstraintsCursor(sugConstr1);
                return;
            }
        }
        else if (Mode==STATUS_SEEK_Second) {
            double dx_ = onSketchPos.fX - EditCurve[0].fX;
            double dy_ = onSketchPos.fY - EditCurve[0].fY;
            for (int i=0; i < 16; i++) {
                double angle = i*M_PI/16.0;
                double dx = dx_ * cos(angle) + dy_ * sin(angle);
                double dy = -dx_ * sin(angle) + dy_ * cos(angle);
                EditCurve[1+i] = Base::Vector2D(EditCurve[0].fX + dx, EditCurve[0].fY + dy);
                EditCurve[17+i] = Base::Vector2D(EditCurve[0].fX - dx, EditCurve[0].fY - dy);
            }
            EditCurve[33] = EditCurve[1];

            // Display radius and start angle
            float radius = (onSketchPos - EditCurve[0]).Length();
            float angle = atan2f(dy_ , dx_);

            SbString text;
            text.sprintf(" (%.1fR,%.1fdeg)", radius, angle * 180 / M_PI);
            setPositionText(onSketchPos, text);

            sketchgui->drawEdit(EditCurve);
            if (seekAutoConstraint(sugConstr2, onSketchPos, Base::Vector2D(0.f,0.f))) {
                renderSuggestConstraintsCursor(sugConstr2);
                return;
            }
        }
        else if (Mode==STATUS_SEEK_Third) {
            double angle1 = atan2(onSketchPos.fY - CenterPoint.fY,
                                 onSketchPos.fX - CenterPoint.fX) - startAngle;
            double angle2 = angle1 + (angle1 < 0. ? 2 : -2) * M_PI ;
            arcAngle = abs(angle1-arcAngle) < abs(angle2-arcAngle) ? angle1 : angle2;
            for (int i=1; i <= 29; i++) {
                double angle = i*arcAngle/29.0;
                double dx = rx * cos(angle) - ry * sin(angle);
                double dy = rx * sin(angle) + ry * cos(angle);
                EditCurve[i] = Base::Vector2D(CenterPoint.fX + dx, CenterPoint.fY + dy);
            }

            // Display radius and arc angle
            float radius = (onSketchPos - EditCurve[0]).Length();

            SbString text;
            text.sprintf(" (%.1fR,%.1fdeg)", radius, arcAngle * 180 / M_PI);
            setPositionText(onSketchPos, text);
            
            sketchgui->drawEdit(EditCurve);
            if (seekAutoConstraint(sugConstr3, onSketchPos, Base::Vector2D(0.0,0.0))) {
                renderSuggestConstraintsCursor(sugConstr3);
                return;
            }
        }
        applyCursor();

    }

    virtual bool pressButton(Base::Vector2D onSketchPos)
    {
        if (Mode==STATUS_SEEK_First){
            CenterPoint = onSketchPos;
            EditCurve.resize(34);
            EditCurve[0] = onSketchPos;
            Mode = STATUS_SEEK_Second;
        }
        else if (Mode==STATUS_SEEK_Second){
            EditCurve.resize(31);
            EditCurve[0] = onSketchPos;
            EditCurve[30] = CenterPoint;
            rx = EditCurve[0].fX - CenterPoint.fX;
            ry = EditCurve[0].fY - CenterPoint.fY;
            startAngle = atan2(ry, rx);
            arcAngle = 0.;
            Mode = STATUS_SEEK_Third;
        }
        else {
            EditCurve.resize(30);
            double angle1 = atan2(onSketchPos.fY - CenterPoint.fY,
                                 onSketchPos.fX - CenterPoint.fX) - startAngle;
            double angle2 = angle1 + (angle1 < 0. ? 2 : -2) * M_PI ;
            arcAngle = abs(angle1-arcAngle) < abs(angle2-arcAngle) ? angle1 : angle2;
            if (arcAngle > 0)
                endAngle = startAngle + arcAngle;
            else {
                endAngle = startAngle;
                startAngle += arcAngle;
            }

            sketchgui->drawEdit(EditCurve);
            applyCursor();
            Mode = STATUS_End;
        }

        return true;
    }

    virtual bool releaseButton(Base::Vector2D onSketchPos)
    {
        if (Mode==STATUS_End) {
            unsetCursor();
            resetPositionText();
            Gui::Command::openCommand("Add sketch arc");
            Gui::Command::doCommand(Gui::Command::Doc,
                "App.ActiveDocument.%s.addGeometry(Part.ArcOfCircle"
                "(Part.Circle(App.Vector(%f,%f,0),App.Vector(0,0,1),%f),"
                "%f,%f))",
                      sketchgui->getObject()->getNameInDocument(),
                      CenterPoint.fX, CenterPoint.fY, sqrt(rx*rx + ry*ry),
                      startAngle, endAngle); //arcAngle > 0 ? 0 : 1);

            Gui::Command::commitCommand();
            Gui::Command::updateActive();

            // Auto Constraint center point
            if (sugConstr1.size() > 0) {
                createAutoConstraints(sugConstr1, getHighestCurveIndex(), Sketcher::mid);
                sugConstr1.clear();
            }

            // Auto Constraint first picked point
            if (sugConstr2.size() > 0) {
                createAutoConstraints(sugConstr2, getHighestCurveIndex(), (arcAngle > 0) ? Sketcher::start : Sketcher::end );
                sugConstr2.clear();
            }

            // Auto Constraint second picked point
            if (sugConstr3.size() > 0) {
                createAutoConstraints(sugConstr3, getHighestCurveIndex(), (arcAngle > 0) ? Sketcher::end : Sketcher::start);
                sugConstr3.clear();
            }

            EditCurve.clear();
            sketchgui->drawEdit(EditCurve);
            sketchgui->purgeHandler(); // no code after this line, Handler get deleted in ViewProvider
        }
        return true;
    }
protected:
    SelectMode Mode;
    std::vector<Base::Vector2D> EditCurve;
    Base::Vector2D CenterPoint;
    double rx, ry, startAngle, endAngle, arcAngle;
    std::vector<AutoConstraint> sugConstr1, sugConstr2, sugConstr3;
};

DEF_STD_CMD_A(CmdSketcherCreateArc);

CmdSketcherCreateArc::CmdSketcherCreateArc()
  : Command("Sketcher_CreateArc")
{
    sAppModule      = "Sketcher";
    sGroup          = QT_TR_NOOP("Sketcher");
    sMenuText       = QT_TR_NOOP("Create arc by center");
    sToolTipText    = QT_TR_NOOP("Create an arc by its center and by its end points");
    sWhatsThis      = sToolTipText;
    sStatusTip      = sToolTipText;
    sPixmap         = "Sketcher_CreateArc";
    eType           = ForEdit;
}

void CmdSketcherCreateArc::activated(int iMsg)
{
    ActivateHandler(getActiveGuiDocument(),new DrawSketchHandlerArc() );
}

bool CmdSketcherCreateArc::isActive(void)
{
    return isCreateGeoActive(getActiveGuiDocument());
}


// ======================================================================================

/* XPM */
static const char *cursor_create3pointarc[]={
"32 32 3 1",
"+ c white",
"# c red",
". c None",
"......+...........###...........",
"......+...........#.#...........",
"......+...........###...........",
"......+..............##.........",
"......+...............##........",
".......................#........",
"+++++...+++++...........#.......",
"........................##......",
"......+..................#......",
"......+..................#......",
"......+...................#.....",
"......+...................#.....",
"......+...................#.....",
"..........................#.....",
"..........................#.....",
"..........................#.....",
"..........................#.....",
".........................#......",
".......................###......",
".......................#.#......",
".......................###......",
"...###.................#........",
"...#.#................#.........",
"...###...............#..........",
"......##...........##...........",
".......###.......##.............",
"..........#######...............",
"................................",
"................................",
"................................",
"................................",
"................................"};

class DrawSketchHandler3PointArc : public DrawSketchHandler
{
public:
    DrawSketchHandler3PointArc()
      : Mode(STATUS_SEEK_First),EditCurve(2){}
    virtual ~DrawSketchHandler3PointArc(){}
    /// mode table
    enum SelectMode {
        STATUS_SEEK_First,      /**< enum value ----. */
        STATUS_SEEK_Second,     /**< enum value ----. */
        STATUS_SEEK_Third,      /**< enum value ----. */
        STATUS_End
    };

    virtual void activated(ViewProviderSketch *sketchgui)
    {
        setCursor(QPixmap(cursor_create3pointarc),7,7);
    }

    virtual void mouseMove(Base::Vector2D onSketchPos)
    {
        if (Mode==STATUS_SEEK_First) {
            setPositionText(onSketchPos);
            if (seekAutoConstraint(sugConstr1, onSketchPos, Base::Vector2D(0.f,0.f))) {
                renderSuggestConstraintsCursor(sugConstr1);
                return;
            }
        }
        else if (Mode==STATUS_SEEK_Second) {
            CenterPoint  = EditCurve[0] = (onSketchPos - FirstPoint)/2 + FirstPoint;
            EditCurve[1] = EditCurve[33] = onSketchPos;
            radius = (onSketchPos - CenterPoint).Length();
            double lineAngle = GetPointAngle(CenterPoint, onSketchPos);

            // Build a 32 point circle ignoring already constructed points
            for (int i=1; i <= 32; i++) {
                // Start at current angle
                double angle = (i-1)*2*M_PI/32.0 + lineAngle; // N point closed circle has N segments
                if (i != 1 && i != 17 ) {
                    EditCurve[i] = Base::Vector2D(CenterPoint.fX + radius*cos(angle),
                                                  CenterPoint.fY + radius*sin(angle));
                }
            }

            // Display radius and start angle
            // This lineAngle will report counter-clockwise from +X, not relatively
            SbString text;
            text.sprintf(" (%.1fR,%.1fdeg)", (float) radius, (float) lineAngle * 180 / M_PI);
            setPositionText(onSketchPos, text);

            sketchgui->drawEdit(EditCurve);
            if (seekAutoConstraint(sugConstr2, onSketchPos, Base::Vector2D(0.f,0.f))) {
                renderSuggestConstraintsCursor(sugConstr2);
                return;
            }
        }
        else if (Mode==STATUS_SEEK_Third) {
            /*
            Centerline inverts when the arc flips sides.  Easily taken care of by replacing
            centerline with a point.  It happens because the direction the curve is being drawn
            reverses.
            */
            CenterPoint = EditCurve[30] = GetCircleCenter(FirstPoint, SecondPoint, onSketchPos);
            radius = (SecondPoint - CenterPoint).Length();

            double angle1 = GetPointAngle(CenterPoint, FirstPoint);
            double angle2 = GetPointAngle(CenterPoint, SecondPoint);
            double angle3 = GetPointAngle(CenterPoint, onSketchPos);

            // Always build arc counter-clockwise
            // Point 3 is between Point 1 and 2
            if ( angle3 > min(angle1, angle2) && angle3 < max(angle1, angle2) ) {
                if (angle2 > angle1) {
                    EditCurve[0] =  FirstPoint;
                    EditCurve[29] = SecondPoint;
                    arcPos1 = Sketcher::start;
                    arcPos2 = Sketcher::end;
                }
                else {
                    EditCurve[0] =  SecondPoint;
                    EditCurve[29] = FirstPoint;
                    arcPos1 = Sketcher::end;
                    arcPos2 = Sketcher::start;
                }
                startAngle = min(angle1, angle2);
                endAngle   = max(angle1, angle2);
                arcAngle = endAngle - startAngle;
            }
            // Point 3 is not between Point 1 and 2
            else {
                if (angle2 > angle1) {
                    EditCurve[0] =  SecondPoint;
                    EditCurve[29] = FirstPoint;
                    arcPos1 = Sketcher::end;
                    arcPos2 = Sketcher::start;
                }
                else {
                    EditCurve[0] =  FirstPoint;
                    EditCurve[29] = SecondPoint;
                    arcPos1 = Sketcher::start;
                    arcPos2 = Sketcher::end;
                }
                startAngle = max(angle1, angle2);
                endAngle   = min(angle1, angle2);
                arcAngle = 2*M_PI - (startAngle - endAngle);
            }

            // Build a 30 point circle ignoring already constructed points
            for (int i=1; i <= 28; i++) {
                double angle = startAngle + i*arcAngle/29.0; // N point arc has N-1 segments
                EditCurve[i] = Base::Vector2D(CenterPoint.fX + radius*cos(angle),
                                              CenterPoint.fY + radius*sin(angle));
            }

            SbString text;
            text.sprintf(" (%.1fR,%.1fdeg)", (float) radius, (float) arcAngle * 180 / M_PI);
            setPositionText(onSketchPos, text);

            sketchgui->drawEdit(EditCurve);
            if (seekAutoConstraint(sugConstr3, onSketchPos, Base::Vector2D(0.0,0.0),
                                   AutoConstraint::CURVE)) {
                renderSuggestConstraintsCursor(sugConstr3);
                return;
            }
        }
        applyCursor();
    }

    virtual bool pressButton(Base::Vector2D onSketchPos)
    {
        if (Mode==STATUS_SEEK_First){
            // 32 point curve + center + endpoint
            EditCurve.resize(34);
            // 17 is circle halfway point (1+32/2)
            FirstPoint = EditCurve[17] = onSketchPos;

            Mode = STATUS_SEEK_Second;
        }
        else if (Mode==STATUS_SEEK_Second){
            // 30 point arc and center point
            EditCurve.resize(31);
            SecondPoint = onSketchPos;

            Mode = STATUS_SEEK_Third;
        }
        else {
            EditCurve.resize(30);

            sketchgui->drawEdit(EditCurve);
            applyCursor();
            Mode = STATUS_End;
        }

        return true;
    }

    virtual bool releaseButton(Base::Vector2D onSketchPos)
    {
        // Need to look at.  rx might need fixing.
        if (Mode==STATUS_End) {
            unsetCursor();
            resetPositionText();
            Gui::Command::openCommand("Add sketch arc");
            Gui::Command::doCommand(Gui::Command::Doc,
                "App.ActiveDocument.%s.addGeometry(Part.ArcOfCircle"
                "(Part.Circle(App.Vector(%f,%f,0),App.Vector(0,0,1),%f),"
                "%f,%f))",
                      sketchgui->getObject()->getNameInDocument(),
                      CenterPoint.fX, CenterPoint.fY, radius,
                      startAngle, endAngle);

            Gui::Command::commitCommand();
            Gui::Command::updateActive();

            // Auto Constraint first picked point
            if (sugConstr1.size() > 0) {
                createAutoConstraints(sugConstr1, getHighestCurveIndex(), arcPos1);
                sugConstr1.clear();
            }

            // Auto Constraint second picked point
            if (sugConstr2.size() > 0) {
                createAutoConstraints(sugConstr2, getHighestCurveIndex(), arcPos2);
                sugConstr2.clear();
            }

            // Auto Constraint third picked point
            if (sugConstr3.size() > 0) {
                createAutoConstraints(sugConstr3, getHighestCurveIndex(), Sketcher::none);
                sugConstr3.clear();
            }

            EditCurve.clear();
            sketchgui->drawEdit(EditCurve);
            sketchgui->purgeHandler(); // no code after this line, Handler get deleted in ViewProvider
        }
        return true;
    }
protected:
    SelectMode Mode;
    std::vector<Base::Vector2D> EditCurve;
    Base::Vector2D CenterPoint, FirstPoint, SecondPoint;
    double radius, startAngle, endAngle, arcAngle;
    std::vector<AutoConstraint> sugConstr1, sugConstr2, sugConstr3;
    Sketcher::PointPos arcPos1, arcPos2;
};

DEF_STD_CMD_A(CmdSketcherCreate3PointArc);

CmdSketcherCreate3PointArc::CmdSketcherCreate3PointArc()
  : Command("Sketcher_Create3PointArc")
{
    sAppModule      = "Sketcher";
    sGroup          = QT_TR_NOOP("Sketcher");
    sMenuText       = QT_TR_NOOP("Create arc by three points");
    sToolTipText    = QT_TR_NOOP("Create an arc by its end points and a point along the arc");
    sWhatsThis      = sToolTipText;
    sStatusTip      = sToolTipText;
    sPixmap         = "Sketcher_Create3PointArc";
    eType           = ForEdit;
}

void CmdSketcherCreate3PointArc::activated(int iMsg)
{
    ActivateHandler(getActiveGuiDocument(),new DrawSketchHandler3PointArc() );
}

bool CmdSketcherCreate3PointArc::isActive(void)
{
    return isCreateGeoActive(getActiveGuiDocument());
}


DEF_STD_CMD_ACL(CmdSketcherCompCreateArc);

CmdSketcherCompCreateArc::CmdSketcherCompCreateArc()
  : Command("Sketcher_CompCreateArc")
{
    sAppModule      = "Sketcher";
    sGroup          = QT_TR_NOOP("Sketcher");
    sMenuText       = QT_TR_NOOP("Create arc");
    sToolTipText    = QT_TR_NOOP("Create an arc in the sketcher");
    sWhatsThis      = sToolTipText;
    sStatusTip      = sToolTipText;
    eType           = ForEdit;
}

void CmdSketcherCompCreateArc::activated(int iMsg)
{
    if (iMsg==0)
        ActivateHandler(getActiveGuiDocument(),new DrawSketchHandlerArc());
    else if (iMsg==1)
        ActivateHandler(getActiveGuiDocument(),new DrawSketchHandler3PointArc());
    else
        return;

    // Since the default icon is reset when enabing/disabling the command we have
    // to explicitly set the icon of the used command.
    Gui::ActionGroup* pcAction = qobject_cast<Gui::ActionGroup*>(_pcAction);
    QList<QAction*> a = pcAction->actions();

    assert(iMsg < a.size());
    pcAction->setIcon(a[iMsg]->icon());
}

Gui::Action * CmdSketcherCompCreateArc::createAction(void)
{
    Gui::ActionGroup* pcAction = new Gui::ActionGroup(this, Gui::getMainWindow());
    pcAction->setDropDownMenu(true);
    applyCommandData(pcAction);

    QAction* arc1 = pcAction->addAction(QString());
    arc1->setIcon(Gui::BitmapFactory().pixmapFromSvg("Sketcher_CreateArc", QSize(24,24)));
    QAction* arc2 = pcAction->addAction(QString());
    arc2->setIcon(Gui::BitmapFactory().pixmapFromSvg("Sketcher_Create3PointArc", QSize(24,24)));

    _pcAction = pcAction;
    languageChange();

    pcAction->setIcon(arc1->icon());
    int defaultId = 0;
    pcAction->setProperty("defaultAction", QVariant(defaultId));

    return pcAction;
}

void CmdSketcherCompCreateArc::languageChange()
{
    Command::languageChange();

    if (!_pcAction)
        return;
    Gui::ActionGroup* pcAction = qobject_cast<Gui::ActionGroup*>(_pcAction);
    QList<QAction*> a = pcAction->actions();

    QAction* arc1 = a[0];
    arc1->setText(QApplication::translate("CmdSketcherCompCreateArc","Center and end points"));
    arc1->setToolTip(QApplication::translate("Sketcher_CreateArc","Create an arc by its center and by its end points"));
    arc1->setStatusTip(QApplication::translate("Sketcher_CreateArc","Create an arc by its center and by its end points"));
    QAction* arc2 = a[1];
    arc2->setText(QApplication::translate("CmdSketcherCompCreateArc","End points and rim point"));
    arc2->setToolTip(QApplication::translate("Sketcher_Create3PointArc","Create an arc by its end points and a point along the arc"));
    arc2->setStatusTip(QApplication::translate("Sketcher_Create3PointArc","Create an arc by its end points and a point along the arc"));
}

bool CmdSketcherCompCreateArc::isActive(void)
{
    return isCreateGeoActive(getActiveGuiDocument());
}


// ======================================================================================

/* XPM */
static const char *cursor_createcircle[]={
"32 32 3 1",
"+ c white",
"# c red",
". c None",
"......+.........................",
"......+.........................",
"......+.........................",
"......+.........................",
"......+.........................",
"................................",
"+++++...+++++...................",
"................................",
"......+........#######..........",
"......+......##.......##........",
"......+.....#...........#.......",
"......+....#.............#......",
"......+...#...............#.....",
".........#.................#....",
"........#...................#...",
"........#...................#...",
".......#.....................#..",
".......#.....................#..",
".......#.........###.........#..",
".......#.........#.#.........#..",
".......#.........###.........#..",
".......#.....................#..",
".......#.....................#..",
"........#...................#...",
"........#...................#...",
".........#.................#....",
"..........#...............#.....",
"...........#.............#......",
"............#...........#.......",
".............##.......##........",
"...............#######..........",
"................................"};

class DrawSketchHandlerCircle : public DrawSketchHandler
{
public:
    DrawSketchHandlerCircle() : Mode(STATUS_SEEK_First),EditCurve(34){}
    virtual ~DrawSketchHandlerCircle(){}
    /// mode table
    enum SelectMode {
        STATUS_SEEK_First,      /**< enum value ----. */
        STATUS_SEEK_Second,     /**< enum value ----. */
        STATUS_Close
    };

    virtual void activated(ViewProviderSketch *sketchgui)
    {
        setCursor(QPixmap(cursor_createcircle),7,7);
    }

    virtual void mouseMove(Base::Vector2D onSketchPos)
    {
        if (Mode==STATUS_SEEK_First) {
            setPositionText(onSketchPos);
            if (seekAutoConstraint(sugConstr1, onSketchPos, Base::Vector2D(0.f,0.f))) {
                renderSuggestConstraintsCursor(sugConstr1);
                return;
            }
        }
        else if (Mode==STATUS_SEEK_Second) {
            double rx0 = onSketchPos.fX - EditCurve[0].fX;
            double ry0 = onSketchPos.fY - EditCurve[0].fY;
            for (int i=0; i < 16; i++) {
                double angle = i*M_PI/16.0;
                double rx = rx0 * cos(angle) + ry0 * sin(angle);
                double ry = -rx0 * sin(angle) + ry0 * cos(angle);
                EditCurve[1+i] = Base::Vector2D(EditCurve[0].fX + rx, EditCurve[0].fY + ry);
                EditCurve[17+i] = Base::Vector2D(EditCurve[0].fX - rx, EditCurve[0].fY - ry);
            }
            EditCurve[33] = EditCurve[1];

            // Display radius for user
            float radius = (onSketchPos - EditCurve[0]).Length();

            SbString text;
            text.sprintf(" (%.1fR)", radius);
            setPositionText(onSketchPos, text);

            sketchgui->drawEdit(EditCurve);
            if (seekAutoConstraint(sugConstr2, onSketchPos, Base::Vector2D(0.f,0.f),
                                   AutoConstraint::CURVE)) {
                renderSuggestConstraintsCursor(sugConstr2);
                return;
            }
        }
        applyCursor();
    }

    virtual bool pressButton(Base::Vector2D onSketchPos)
    {
        if (Mode==STATUS_SEEK_First){
            EditCurve[0] = onSketchPos;
            Mode = STATUS_SEEK_Second;
        } else {
            EditCurve[1] = onSketchPos;
            Mode = STATUS_Close;
        }
        return true;
    }

    virtual bool releaseButton(Base::Vector2D onSketchPos)
    {
        if (Mode==STATUS_Close) {
            double rx = EditCurve[1].fX - EditCurve[0].fX;
            double ry = EditCurve[1].fY - EditCurve[0].fY;
            unsetCursor();
            resetPositionText();
            Gui::Command::openCommand("Add sketch circle");
            Gui::Command::doCommand(Gui::Command::Doc,
                "App.ActiveDocument.%s.addGeometry(Part.Circle"
                "(App.Vector(%f,%f,0),App.Vector(0,0,1),%f))",
                      sketchgui->getObject()->getNameInDocument(),
                      EditCurve[0].fX, EditCurve[0].fY,
                      sqrt(rx*rx + ry*ry));

            Gui::Command::commitCommand();
            Gui::Command::updateActive();

            // add auto constraints for the center point
            if (sugConstr1.size() > 0) {
                createAutoConstraints(sugConstr1, getHighestCurveIndex(), Sketcher::mid);
                sugConstr1.clear();
            }

            // add suggested constraints for circumference
            if (sugConstr2.size() > 0) {
                createAutoConstraints(sugConstr2, getHighestCurveIndex(), Sketcher::none);
                sugConstr2.clear();
            }

            EditCurve.clear();
            sketchgui->drawEdit(EditCurve);
            sketchgui->purgeHandler(); // no code after this line, Handler get deleted in ViewProvider
        }
        return true;
    }
protected:
    SelectMode Mode;
    std::vector<Base::Vector2D> EditCurve;
    std::vector<AutoConstraint> sugConstr1, sugConstr2;

};

DEF_STD_CMD_A(CmdSketcherCreateCircle);

CmdSketcherCreateCircle::CmdSketcherCreateCircle()
  : Command("Sketcher_CreateCircle")
{
    sAppModule      = "Sketcher";
    sGroup          = QT_TR_NOOP("Sketcher");
    sMenuText       = QT_TR_NOOP("Create circle");
    sToolTipText    = QT_TR_NOOP("Create a circle in the sketch");
    sWhatsThis      = sToolTipText;
    sStatusTip      = sToolTipText;
    sPixmap         = "Sketcher_CreateCircle";
    eType           = ForEdit;
}

void CmdSketcherCreateCircle::activated(int iMsg)
{
    ActivateHandler(getActiveGuiDocument(),new DrawSketchHandlerCircle() );
}

bool CmdSketcherCreateCircle::isActive(void)
{
    return isCreateGeoActive(getActiveGuiDocument());
}


// ======================================================================================

/* XPM */
static const char *cursor_create3pointcircle[]={
"32 32 3 1",
"+ c white",
"# c red",
". c None",
"......+.........................",
"......+.........................",
"......+.........................",
"......+.........................",
"......+.........................",
"................................",
"+++++...+++++...................",
"................................",
"......+........#######..........",
"......+......##.......##........",
"......+.....#...........#.......",
"......+....#.............#......",
"......+...#...............#.....",
".........#.................#....",
".......###.................###..",
".......#.#.................#.#..",
".......###.................###..",
".......#.....................#..",
".......#.........###.........#..",
".......#.........#.#.........#..",
".......#.........###.........#..",
".......#.....................#..",
".......#.....................#..",
"........#...................#...",
"........#...................#...",
".........#.................#....",
"..........#...............#.....",
"...........#.............#......",
"............#...........#.......",
".............##..###..##........",
"...............###.###..........",
".................###............"};

class DrawSketchHandler3PointCircle : public DrawSketchHandler
{
public:
    DrawSketchHandler3PointCircle()
      : Mode(STATUS_SEEK_First),EditCurve(2),N(32.0){}
    virtual ~DrawSketchHandler3PointCircle(){}
    /// mode table
    enum SelectMode {
        STATUS_SEEK_First,      /**< enum value ----. */
        STATUS_SEEK_Second,     /**< enum value ----. */
        STATUS_SEEK_Third,      /**< enum value ----. */
        STATUS_End
    };

    virtual void activated(ViewProviderSketch *sketchgui)
    {
        setCursor(QPixmap(cursor_create3pointcircle),7,7);
    }

    virtual void mouseMove(Base::Vector2D onSketchPos)
    {
        if (Mode == STATUS_SEEK_First) {
            setPositionText(onSketchPos);
            if (seekAutoConstraint(sugConstr1, onSketchPos, Base::Vector2D(0.f,0.f), 
                                   AutoConstraint::CURVE)) {
                // Disable tangent snap on 1st point
                if (sugConstr1.back().Type == Sketcher::Tangent)
                    sugConstr1.pop_back();
                else
                    renderSuggestConstraintsCursor(sugConstr1);
                return;
            }
        }
        else if (Mode == STATUS_SEEK_Second || Mode == STATUS_SEEK_Third) {
            if (Mode == STATUS_SEEK_Second)
                CenterPoint  = EditCurve[N+1] = (onSketchPos - FirstPoint)/2 + FirstPoint;
            else
                CenterPoint = EditCurve[N+1] = GetCircleCenter(FirstPoint, SecondPoint, onSketchPos);
            radius = (onSketchPos - CenterPoint).Length();
            double lineAngle = GetPointAngle(CenterPoint, onSketchPos);

            // Build a N point circle
            for (int i=1; i < N; i++) {
                // Start at current angle
                double angle = i*2*M_PI/N + lineAngle; // N point closed circle has N segments
                EditCurve[i] = Base::Vector2D(CenterPoint.fX + radius*cos(angle),
                                              CenterPoint.fY + radius*sin(angle));
            }
            // Beginning and end of curve should be exact
            EditCurve[0] = EditCurve[N] = onSketchPos;
            
            // Display radius and start angle
            // This lineAngle will report counter-clockwise from +X, not relatively
            SbString text;
            text.sprintf(" (%.1fR,%.1fdeg)", (float) radius, (float) lineAngle * 180 / M_PI);
            setPositionText(onSketchPos, text);

            sketchgui->drawEdit(EditCurve);
            if (Mode == STATUS_SEEK_Second) {
                if (seekAutoConstraint(sugConstr2, onSketchPos, Base::Vector2D(0.f,0.f), 
                                       AutoConstraint::CURVE)) {
                    // Disable tangent snap on 2nd point
                    if (sugConstr2.back().Type == Sketcher::Tangent)
                        sugConstr2.pop_back();
                    else
                        renderSuggestConstraintsCursor(sugConstr2);
                    return;
                }
            }
            else {
                if (seekAutoConstraint(sugConstr3, onSketchPos, Base::Vector2D(0.0,0.0), 
                                       AutoConstraint::CURVE)) {
                    renderSuggestConstraintsCursor(sugConstr3);
                    return;
                }
            }
        }
        applyCursor();
    }

    virtual bool pressButton(Base::Vector2D onSketchPos)
    {
        if (Mode == STATUS_SEEK_First) {
            // N point curve + center + endpoint
            EditCurve.resize(N+2);
            FirstPoint = onSketchPos;

            Mode = STATUS_SEEK_Second;
        }
        else if (Mode == STATUS_SEEK_Second) {
            SecondPoint = onSketchPos;

            Mode = STATUS_SEEK_Third;
        }
        else {
            EditCurve.resize(N);

            sketchgui->drawEdit(EditCurve);
            applyCursor();
            Mode = STATUS_End;
        }

        return true;
    }

    virtual bool releaseButton(Base::Vector2D onSketchPos)
    {
        // Need to look at.  rx might need fixing.
        if (Mode==STATUS_End) {
            unsetCursor();
            resetPositionText();
            Gui::Command::openCommand("Add sketch circle");
            Gui::Command::doCommand(Gui::Command::Doc,
                "App.ActiveDocument.%s.addGeometry(Part.Circle"
                "(App.Vector(%f,%f,0),App.Vector(0,0,1),%f))",
                      sketchgui->getObject()->getNameInDocument(),
                      CenterPoint.fX, CenterPoint.fY,
                      radius);

            Gui::Command::commitCommand();
            Gui::Command::updateActive();

            // Auto Constraint first picked point
            if (sugConstr1.size() > 0) {
                createAutoConstraints(sugConstr1, getHighestCurveIndex(), Sketcher::none);
                sugConstr1.clear();
            }

            // Auto Constraint second picked point
            if (sugConstr2.size() > 0) {
                createAutoConstraints(sugConstr2, getHighestCurveIndex(), Sketcher::none);
                sugConstr2.clear();
            }

            // Auto Constraint third picked point
            if (sugConstr3.size() > 0) {
                createAutoConstraints(sugConstr3, getHighestCurveIndex(), Sketcher::none);
                sugConstr3.clear();
            }

            EditCurve.clear();
            sketchgui->drawEdit(EditCurve);
            sketchgui->purgeHandler(); // no code after this line, Handler get deleted in ViewProvider
        }
        return true;
    }
protected:
    SelectMode Mode;
    std::vector<Base::Vector2D> EditCurve;
    Base::Vector2D CenterPoint, FirstPoint, SecondPoint;
    double radius, N; // N should be even
    std::vector<AutoConstraint> sugConstr1, sugConstr2, sugConstr3;
};

DEF_STD_CMD_A(CmdSketcherCreate3PointCircle);

CmdSketcherCreate3PointCircle::CmdSketcherCreate3PointCircle()
  : Command("Sketcher_Create3PointCircle")
{
    sAppModule      = "Sketcher";
    sGroup          = QT_TR_NOOP("Sketcher");
    sMenuText       = QT_TR_NOOP("Create circle by three points");
    sToolTipText    = QT_TR_NOOP("Create a circle by 3 perimeter points");
    sWhatsThis      = sToolTipText;
    sStatusTip      = sToolTipText;
    sPixmap         = "Sketcher_Create3PointCircle";
    eType           = ForEdit;
}

void CmdSketcherCreate3PointCircle::activated(int iMsg)
{
    ActivateHandler(getActiveGuiDocument(),new DrawSketchHandler3PointCircle() );
}

bool CmdSketcherCreate3PointCircle::isActive(void)
{
    return isCreateGeoActive(getActiveGuiDocument());
}


DEF_STD_CMD_ACL(CmdSketcherCompCreateCircle);

CmdSketcherCompCreateCircle::CmdSketcherCompCreateCircle()
  : Command("Sketcher_CompCreateCircle")
{
    sAppModule      = "Sketcher";
    sGroup          = QT_TR_NOOP("Sketcher");
    sMenuText       = QT_TR_NOOP("Create circle");
    sToolTipText    = QT_TR_NOOP("Create a circle in the sketcher");
    sWhatsThis      = sToolTipText;
    sStatusTip      = sToolTipText;
    eType           = ForEdit;
}

void CmdSketcherCompCreateCircle::activated(int iMsg)
{
    if (iMsg==0)
        ActivateHandler(getActiveGuiDocument(),new DrawSketchHandlerCircle());
    else if (iMsg==1)
        ActivateHandler(getActiveGuiDocument(),new DrawSketchHandler3PointCircle());
    else
        return;

    // Since the default icon is reset when enabing/disabling the command we have
    // to explicitly set the icon of the used command.
    Gui::ActionGroup* pcAction = qobject_cast<Gui::ActionGroup*>(_pcAction);
    QList<QAction*> a = pcAction->actions();

    assert(iMsg < a.size());
    pcAction->setIcon(a[iMsg]->icon());
}

Gui::Action * CmdSketcherCompCreateCircle::createAction(void)
{
    Gui::ActionGroup* pcAction = new Gui::ActionGroup(this, Gui::getMainWindow());
    pcAction->setDropDownMenu(true);
    applyCommandData(pcAction);

    QAction* arc1 = pcAction->addAction(QString());
    arc1->setIcon(Gui::BitmapFactory().pixmapFromSvg("Sketcher_CreateCircle", QSize(24,24)));
    QAction* arc2 = pcAction->addAction(QString());
    arc2->setIcon(Gui::BitmapFactory().pixmapFromSvg("Sketcher_Create3PointCircle", QSize(24,24)));

    _pcAction = pcAction;
    languageChange();

    pcAction->setIcon(arc1->icon());
    int defaultId = 0;
    pcAction->setProperty("defaultAction", QVariant(defaultId));

    return pcAction;
}

void CmdSketcherCompCreateCircle::languageChange()
{
    Command::languageChange();

    if (!_pcAction)
        return;
    Gui::ActionGroup* pcAction = qobject_cast<Gui::ActionGroup*>(_pcAction);
    QList<QAction*> a = pcAction->actions();

    QAction* arc1 = a[0];
    arc1->setText(QApplication::translate("CmdSketcherCompCreateCircle", "Center and rim point"));
    arc1->setToolTip(QApplication::translate("Sketcher_CreateCircle", "Create a circle by its center and by a rim point"));
    arc1->setStatusTip(QApplication::translate("Sketcher_CreateCircle", "Create a circle by its center and by a rim point"));
    QAction* arc2 = a[1];
    arc2->setText(QApplication::translate("CmdSketcherCompCreateCircle", "3 rim points"));
    arc2->setToolTip(QApplication::translate("Sketcher_Create3PointCircle", "Create a circle by 3 rim points"));
    arc2->setStatusTip(QApplication::translate("Sketcher_Create3PointCircle", "Create a circle by 3 rim points"));
}

bool CmdSketcherCompCreateCircle::isActive(void)
{
    return isCreateGeoActive(getActiveGuiDocument());
}
    

// ======================================================================================

/* XPM */
static const char *cursor_createpoint[]={
"32 32 3 1",
"+ c white",
"# c red",
". c None",
"......+.........................",
"......+.........................",
"......+.........................",
"......+.........................",
"......+.........................",
"................................",
"+++++...+++++...................",
"................................",
"......+.........................",
"......+.........................",
"......+.........................",
"......+.........................",
"......+.........................",
"................................",
"................................",
"................................",
".................++++...........",
"................++++++..........",
"...............++++++++.........",
"...............++++++++.........",
"...............++++++++.........",
"...............++++++++.........",
"................++++++..........",
".................++++...........",
"................................",
"................................",
"................................",
"................................",
"................................",
"................................",
"................................",
"................................"};

class DrawSketchHandlerPoint: public DrawSketchHandler
{
public:
    DrawSketchHandlerPoint() : selectionDone(false) {}
    virtual ~DrawSketchHandlerPoint() {}

    virtual void activated(ViewProviderSketch *sketchgui)
    {
        setCursor(QPixmap(cursor_createpoint),7,7);
    }

    virtual void mouseMove(Base::Vector2D onSketchPos)
    {
        setPositionText(onSketchPos);
        if (seekAutoConstraint(sugConstr, onSketchPos, Base::Vector2D(0.f,0.f))) {
            renderSuggestConstraintsCursor(sugConstr);
            return;
        }
        applyCursor();
    }

    virtual bool pressButton(Base::Vector2D onSketchPos)
    {
        EditPoint = onSketchPos;
        selectionDone = true;
        return true;
    }

    virtual bool releaseButton(Base::Vector2D onSketchPos)
    {
        if (selectionDone){
            unsetCursor();
            resetPositionText();

            Gui::Command::openCommand("Add sketch point");
            Gui::Command::doCommand(Gui::Command::Doc,"App.ActiveDocument.%s.addGeometry(Part.Point(App.Vector(%f,%f,0)))",
                      sketchgui->getObject()->getNameInDocument(),
                      EditPoint.fX,EditPoint.fY);
            Gui::Command::commitCommand();
            Gui::Command::updateActive();

            // add auto constraints for the line segment start
            if (sugConstr.size() > 0) {
                createAutoConstraints(sugConstr, getHighestCurveIndex(), Sketcher::start);
                sugConstr.clear();
            }

            sketchgui->purgeHandler(); // no code after this line, Handler get deleted in ViewProvider
        }
        return true;
    }
protected:
    bool selectionDone;
    Base::Vector2D EditPoint;
    std::vector<AutoConstraint> sugConstr;
};

DEF_STD_CMD_A(CmdSketcherCreatePoint);

CmdSketcherCreatePoint::CmdSketcherCreatePoint()
  : Command("Sketcher_CreatePoint")
{
    sAppModule      = "Sketcher";
    sGroup          = QT_TR_NOOP("Sketcher");
    sMenuText       = QT_TR_NOOP("Create point");
    sToolTipText    = QT_TR_NOOP("Create a point in the sketch");
    sWhatsThis      = sToolTipText;
    sStatusTip      = sToolTipText;
    sPixmap         = "Sketcher_CreatePoint";
    eType           = ForEdit;
}

void CmdSketcherCreatePoint::activated(int iMsg)
{
    ActivateHandler(getActiveGuiDocument(), new DrawSketchHandlerPoint());
}

bool CmdSketcherCreatePoint::isActive(void)
{
    return isCreateGeoActive(getActiveGuiDocument());
}


// ======================================================================================

DEF_STD_CMD_A(CmdSketcherCreateText);

CmdSketcherCreateText::CmdSketcherCreateText()
  : Command("Sketcher_CreateText")
{
    sAppModule      = "Sketcher";
    sGroup          = QT_TR_NOOP("Sketcher");
    sMenuText       = QT_TR_NOOP("Create text");
    sToolTipText    = QT_TR_NOOP("Create text in the sketch");
    sWhatsThis      = sToolTipText;
    sStatusTip      = sToolTipText;
    sPixmap         = "Sketcher_CreateText";
    eType           = ForEdit;
}

void CmdSketcherCreateText::activated(int iMsg)
{
}

bool CmdSketcherCreateText::isActive(void)
{
    return false;
}


// ======================================================================================

DEF_STD_CMD_A(CmdSketcherCreateDraftLine);

CmdSketcherCreateDraftLine::CmdSketcherCreateDraftLine()
  : Command("Sketcher_CreateDraftLine")
{
    sAppModule      = "Sketcher";
    sGroup          = QT_TR_NOOP("Sketcher");
    sMenuText       = QT_TR_NOOP("Create draft line");
    sToolTipText    = QT_TR_NOOP("Create a draft line in the sketch");
    sWhatsThis      = sToolTipText;
    sStatusTip      = sToolTipText;
    sPixmap         = "Sketcher_DraftLine";
    eType           = ForEdit;
}

void CmdSketcherCreateDraftLine::activated(int iMsg)
{
}

bool CmdSketcherCreateDraftLine::isActive(void)
{
    return false;
}


// ======================================================================================

namespace SketcherGui {
    class FilletSelection : public Gui::SelectionFilterGate
    {
        App::DocumentObject* object;
    public:
        FilletSelection(App::DocumentObject* obj)
            : Gui::SelectionFilterGate((Gui::SelectionFilter*)0), object(obj)
        {}

        bool allow(App::Document *pDoc, App::DocumentObject *pObj, const char *sSubName)
        {
            if (pObj != this->object)
                return false;
            if (!sSubName || sSubName[0] == '\0')
                return false;
            std::string element(sSubName);
            if (element.substr(0,4) == "Edge") {
                int GeoId = std::atoi(element.substr(4,4000).c_str()) - 1;
                Sketcher::SketchObject *Sketch = static_cast<Sketcher::SketchObject*>(object);
                const Part::Geometry *geom = Sketch->getGeometry(GeoId);
                if (geom->getTypeId() == Part::GeomLineSegment::getClassTypeId())
                    return true;
            }
            if (element.substr(0,6) == "Vertex") {
                int VtId = std::atoi(element.substr(6,4000).c_str()) - 1;
                Sketcher::SketchObject *Sketch = static_cast<Sketcher::SketchObject*>(object);
                std::vector<int> GeoIdList;
                std::vector<Sketcher::PointPos> PosIdList;
                Sketch->getCoincidentPoints(VtId, GeoIdList, PosIdList);
                if (GeoIdList.size() == 2 && GeoIdList[0] >= 0  && GeoIdList[1] >= 0) {
                    const Part::Geometry *geom1 = Sketch->getGeometry(GeoIdList[0]);
                    const Part::Geometry *geom2 = Sketch->getGeometry(GeoIdList[1]);
                    if (geom1->getTypeId() == Part::GeomLineSegment::getClassTypeId() &&
                        geom2->getTypeId() == Part::GeomLineSegment::getClassTypeId())
                        return true;
                }
            }
            return  false;
        }
    };
};


/* XPM */
static const char *cursor_createfillet[]={
"32 32 3 1",
"+ c white",
"* c red",
". c None",
"......+.........................",
"......+.........................",
"......+.........................",
"......+.........................",
"......+.........................",
"................................",
"+++++...+++++...................",
"................................",
"......+.........................",
"......+.........................",
"......+.........................",
"......+.........................",
"......+..*......................",
".........*......................",
".........*......................",
".........*......................",
".........*......................",
".........*......................",
".........*.........***..........",
".........*.........*.*..........",
".........*.........***..........",
".........*......................",
".........*......................",
"..........*.....................",
"..........*.....................",
"...........*....................",
"............*...................",
".............*..................",
"..............*.................",
"...............**...............",
".................**************.",
"................................"};

class DrawSketchHandlerFillet: public DrawSketchHandler
{
public:
    DrawSketchHandlerFillet() : Mode(STATUS_SEEK_First) {}
    virtual ~DrawSketchHandlerFillet()
    {
        Gui::Selection().rmvSelectionGate();
    }
    enum SelectMode{
        STATUS_SEEK_First,
        STATUS_SEEK_Second
    };

    virtual void activated(ViewProviderSketch *sketchgui)
    {
        Gui::Selection().rmvSelectionGate();
        Gui::Selection().addSelectionGate(new FilletSelection(sketchgui->getObject()));
        setCursor(QPixmap(cursor_createfillet),7,7);
    }

    virtual void mouseMove(Base::Vector2D onSketchPos)
    {
    }

    virtual bool pressButton(Base::Vector2D onSketchPos)
    {
        return true;
    }

    virtual bool releaseButton(Base::Vector2D onSketchPos)
    {
        int VtId = sketchgui->getPreselectPoint();
        if (Mode == STATUS_SEEK_First && VtId != -1) {
            int GeoId;
            Sketcher::PointPos PosId=Sketcher::none;
            sketchgui->getSketchObject()->getGeoVertexIndex(VtId,GeoId,PosId);
            const Part::Geometry *geom = sketchgui->getSketchObject()->getGeometry(GeoId);
            if (geom->getTypeId() == Part::GeomLineSegment::getClassTypeId() &&
                (PosId == Sketcher::start || PosId == Sketcher::end)) {

                // guess fillet radius
                double radius=-1;
                std::vector<int> GeoIdList;
                std::vector<Sketcher::PointPos> PosIdList;
                sketchgui->getSketchObject()->getCoincidentPoints(GeoId, PosId, GeoIdList, PosIdList);
                if (GeoIdList.size() == 2 && GeoIdList[0] >= 0  && GeoIdList[1] >= 0) {
                    const Part::Geometry *geom1 = sketchgui->getSketchObject()->getGeometry(GeoIdList[0]);
                    const Part::Geometry *geom2 = sketchgui->getSketchObject()->getGeometry(GeoIdList[1]);
                    if (geom1->getTypeId() == Part::GeomLineSegment::getClassTypeId() &&
                        geom2->getTypeId() == Part::GeomLineSegment::getClassTypeId()) {
                        const Part::GeomLineSegment *lineSeg1 = dynamic_cast<const Part::GeomLineSegment *>(geom1);
                        const Part::GeomLineSegment *lineSeg2 = dynamic_cast<const Part::GeomLineSegment *>(geom2);
                        Base::Vector3d dir1 = lineSeg1->getEndPoint() - lineSeg1->getStartPoint();
                        Base::Vector3d dir2 = lineSeg2->getEndPoint() - lineSeg2->getStartPoint();
                        if (PosIdList[0] == Sketcher::end)
                            dir1 *= -1;
                        if (PosIdList[1] == Sketcher::end)
                            dir2 *= -1;
                        double l1 = dir1.Length();
                        double l2 = dir2.Length();
                        double angle = dir1.GetAngle(dir2);
                        radius = (l1 < l2 ? l1 : l2) * 0.2 * sin(angle/2);
                    }
                }
                if (radius < 0)
                    return false;

                // create fillet at point
                Gui::Command::openCommand("Create fillet");
                Gui::Command::doCommand(Gui::Command::Doc,"App.ActiveDocument.%s.fillet(%d,%d,%f)",
                          sketchgui->getObject()->getNameInDocument(),
                          GeoId, PosId, radius);
                Gui::Command::commitCommand();
                Gui::Command::updateActive();
            }
            return true;
        }

        int GeoId = sketchgui->getPreselectCurve();
        if (GeoId > -1) {
            const Part::Geometry *geom = sketchgui->getSketchObject()->getGeometry(GeoId);
            if (geom->getTypeId() == Part::GeomLineSegment::getClassTypeId()) {
                if (Mode==STATUS_SEEK_First) {
                    firstCurve = GeoId;
                    firstPos = onSketchPos;
                    Mode = STATUS_SEEK_Second;
                    // add the line to the selection
                    std::stringstream ss;
                    ss << "Edge" << firstCurve + 1;
                    Gui::Selection().addSelection(sketchgui->getSketchObject()->getDocument()->getName()
                                                 ,sketchgui->getSketchObject()->getNameInDocument()
                                                 ,ss.str().c_str()
                                                 ,onSketchPos.fX
                                                 ,onSketchPos.fY
                                                 ,0.f);
                }
                else if (Mode==STATUS_SEEK_Second) {
                    int secondCurve = GeoId;
                    Base::Vector2D secondPos = onSketchPos;

                    // guess fillet radius
                    const Part::GeomLineSegment *lineSeg1 = dynamic_cast<const Part::GeomLineSegment *>
                                                            (sketchgui->getSketchObject()->getGeometry(firstCurve));
                    const Part::GeomLineSegment *lineSeg2 = dynamic_cast<const Part::GeomLineSegment *>
                                                            (sketchgui->getSketchObject()->getGeometry(secondCurve));
                    Base::Vector3d refPnt1(firstPos.fX, firstPos.fY, 0.f);
                    Base::Vector3d refPnt2(secondPos.fX, secondPos.fY, 0.f);
                    double radius = Part::suggestFilletRadius(lineSeg1, lineSeg2, refPnt1, refPnt2);
                    if (radius < 0)
                        return false;

                    // create fillet between lines
                    Gui::Command::openCommand("Create fillet");
                    Gui::Command::doCommand(Gui::Command::Doc,"App.ActiveDocument.%s.fillet(%d,%d,App.Vector(%f,%f,0),App.Vector(%f,%f,0),%f)",
                              sketchgui->getObject()->getNameInDocument(),
                              firstCurve, secondCurve,
                              firstPos.fX, firstPos.fY,
                              secondPos.fX, secondPos.fY, radius);
                    Gui::Command::commitCommand();
                    Gui::Command::updateActive();

                    Gui::Selection().clearSelection();
                    Mode = STATUS_SEEK_First;
                }
            }
        }

        if (VtId < 0 && GeoId < 0) // exit the fillet tool if the user clicked on empty space
            sketchgui->purgeHandler(); // no code after this line, Handler get deleted in ViewProvider

        return true;
    }

protected:
    SelectMode Mode;
    int firstCurve;
    Base::Vector2D firstPos;
};

DEF_STD_CMD_A(CmdSketcherCreateFillet);

CmdSketcherCreateFillet::CmdSketcherCreateFillet()
  : Command("Sketcher_CreateFillet")
{
    sAppModule      = "Sketcher";
    sGroup          = QT_TR_NOOP("Sketcher");
    sMenuText       = QT_TR_NOOP("Create fillet");
    sToolTipText    = QT_TR_NOOP("Create a fillet between two lines or at a coincident point");
    sWhatsThis      = sToolTipText;
    sStatusTip      = sToolTipText;
    sPixmap         = "Sketcher_CreateFillet";
    sAccel          = "F";
    eType           = ForEdit;
}

void CmdSketcherCreateFillet::activated(int iMsg)
{
    ActivateHandler(getActiveGuiDocument(), new DrawSketchHandlerFillet());
}

bool CmdSketcherCreateFillet::isActive(void)
{
    return isCreateGeoActive(getActiveGuiDocument());
}


// ======================================================================================

namespace SketcherGui {
    class TrimmingSelection : public Gui::SelectionFilterGate
    {
        App::DocumentObject* object;
    public:
        TrimmingSelection(App::DocumentObject* obj)
            : Gui::SelectionFilterGate((Gui::SelectionFilter*)0), object(obj)
        {}

        bool allow(App::Document *pDoc, App::DocumentObject *pObj, const char *sSubName)
        {
            if (pObj != this->object)
                return false;
            if (!sSubName || sSubName[0] == '\0')
                return false;
            std::string element(sSubName);
            if (element.substr(0,4) == "Edge") {
                int GeoId = std::atoi(element.substr(4,4000).c_str()) - 1;
                Sketcher::SketchObject *Sketch = static_cast<Sketcher::SketchObject*>(object);
                const Part::Geometry *geom = Sketch->getGeometry(GeoId);
                if (geom->getTypeId() == Part::GeomLineSegment::getClassTypeId() ||
                    geom->getTypeId() == Part::GeomCircle::getClassTypeId()||
                    geom->getTypeId() == Part::GeomArcOfCircle::getClassTypeId())
                    return true;
            }
            return  false;
        }
    };
};


/* XPM */
static const char *cursor_trimming[]={
"32 32 3 1",
"+ c white",
"* c red",
". c None",
"......+.........................",
"......+.........................",
"......+.........................",
"......+.........................",
"......+.........................",
"................................",
"+++++...+++++...................",
"................................",
"......+.........................",
"......+.........................",
"......+......................*..",
"......+....................**...",
"......+...................**....",
".*..............................",
"..*.....................*.......",
"...*..................**........",
".....*...............**.........",
"......*.........................",
".......*..........*.............",
".........*......**..............",
"..........*....**...............",
"...........****.................",
"............*.*.................",
"............***.................",
"..........*....*................",
".........*.......*..............",
".......*..........*.............",
"......*............*............",
"....*................*..........",
"...*..................*.........",
".*.....................*........",
".........................*......"};

class DrawSketchHandlerTrimming: public DrawSketchHandler
{
public:
    DrawSketchHandlerTrimming() {}
    virtual ~DrawSketchHandlerTrimming()
    {
        Gui::Selection().rmvSelectionGate();
    }

    virtual void activated(ViewProviderSketch *sketchgui)
    {
        Gui::Selection().clearSelection();
        Gui::Selection().rmvSelectionGate();
        Gui::Selection().addSelectionGate(new TrimmingSelection(sketchgui->getObject()));
        setCursor(QPixmap(cursor_trimming),7,7);
    }

    virtual void mouseMove(Base::Vector2D onSketchPos)
    {
    }

    virtual bool pressButton(Base::Vector2D onSketchPos)
    {
        return true;
    }

    virtual bool releaseButton(Base::Vector2D onSketchPos)
    {
        int GeoId = sketchgui->getPreselectCurve();
        if (GeoId > -1) {
            const Part::Geometry *geom = sketchgui->getSketchObject()->getGeometry(GeoId);
            if (geom->getTypeId() == Part::GeomLineSegment::getClassTypeId() ||
                geom->getTypeId() == Part::GeomArcOfCircle::getClassTypeId() ||
                geom->getTypeId() == Part::GeomCircle::getClassTypeId()
            ) {
                try {
                    Gui::Command::openCommand("Trim edge");
                    Gui::Command::doCommand(Gui::Command::Doc,"App.ActiveDocument.%s.trim(%d,App.Vector(%f,%f,0))",
                              sketchgui->getObject()->getNameInDocument(),
                              GeoId, onSketchPos.fX, onSketchPos.fY);
                    Gui::Command::commitCommand();
                    Gui::Command::updateActive();
                }
                catch (const Base::Exception& e) {
                    Base::Console().Error("%s\n", e.what());
                }
            }
        }
        else // exit the trimming tool if the user clicked on empty space
            sketchgui->purgeHandler(); // no code after this line, Handler get deleted in ViewProvider

        return true;
    }
};

DEF_STD_CMD_A(CmdSketcherTrimming);

CmdSketcherTrimming::CmdSketcherTrimming()
  : Command("Sketcher_Trimming")
{
    sAppModule      = "Sketcher";
    sGroup          = QT_TR_NOOP("Sketcher");
    sMenuText       = QT_TR_NOOP("Trim edge");
    sToolTipText    = QT_TR_NOOP("Trim an edge with respect to the picked position");
    sWhatsThis      = sToolTipText;
    sStatusTip      = sToolTipText;
    sPixmap         = "Sketcher_Trimming";
    sAccel          = "T";
    eType           = ForEdit;
}

void CmdSketcherTrimming::activated(int iMsg)
{
    ActivateHandler(getActiveGuiDocument(), new DrawSketchHandlerTrimming());
}

bool CmdSketcherTrimming::isActive(void)
{
    return isCreateGeoActive(getActiveGuiDocument());
}


// ======================================================================================

namespace SketcherGui {
    class ExternalSelection : public Gui::SelectionFilterGate
    {
        App::DocumentObject* object;
    public:
        ExternalSelection(App::DocumentObject* obj)
            : Gui::SelectionFilterGate((Gui::SelectionFilter*)0), object(obj)
        {}

        bool allow(App::Document *pDoc, App::DocumentObject *pObj, const char *sSubName)
        {
            Sketcher::SketchObject *sketch = static_cast<Sketcher::SketchObject*>(object);
            App::DocumentObject *support = sketch->Support.getValue();
            // for the moment we allow external constraints only from the support
            if (pObj != support)
                return false;
            if (!sSubName || sSubName[0] == '\0')
                return false;
            std::string element(sSubName);
            // for the moment we allow only edges and vertices
            if ((element.size() > 4 && element.substr(0,4) == "Edge") ||
                (element.size() > 6 && element.substr(0,6) == "Vertex")) {
                return true;
            }
            return  false;
        }
    };
};


/* XPM */
static const char *cursor_external[]={
"32 32 3 1",
"+ c white",
"* c red",
". c None",
"......+.........................",
"......+.........................",
"......+.........................",
"......+.........................",
"......+.........................",
"................................",
"+++++...+++++...................",
"................................",
"......+.........................",
"......+.........................",
"......+.........................",
"......+.........................",
"......+....***************......",
".........**...............***...",
"........**................***...",
".......**................**.*...",
"......*.................*...*...",
"....**................**....*...",
"...**................**.....*...",
"..**................**......*...",
"..******************........*...",
"..*................*........*...",
"..*................*........*...",
"..*................*........*...",
"..*................*............",
"..*................*............",
"..*................*............",
"..*................*............",
"..*................*............",
"..*................*............",
"................................",
"................................"};

class DrawSketchHandlerExternal: public DrawSketchHandler
{
public:
    DrawSketchHandlerExternal() {}
    virtual ~DrawSketchHandlerExternal()
    {
        Gui::Selection().rmvSelectionGate();
    }

    virtual void activated(ViewProviderSketch *sketchgui)
    {
        sketchgui->setAxisPickStyle(false);
        Gui::MDIView *mdi = Gui::Application::Instance->activeDocument()->getActiveView();
        Gui::View3DInventorViewer *viewer;
        viewer = static_cast<Gui::View3DInventor *>(mdi)->getViewer();

        SoNode* root = viewer->getSceneGraph();
        static_cast<Gui::SoFCUnifiedSelection*>(root)->selectionRole.setValue(TRUE);

        Gui::Selection().clearSelection();
        Gui::Selection().rmvSelectionGate();
        Gui::Selection().addSelectionGate(new ExternalSelection(sketchgui->getObject()));
        setCursor(QPixmap(cursor_external),7,7);
    }

    virtual void deactivated(ViewProviderSketch *sketchgui)
    {
        sketchgui->setAxisPickStyle(true);
    }

    virtual void mouseMove(Base::Vector2D onSketchPos)
    {
        if (Gui::Selection().getPreselection().pObjectName)
            applyCursor();
    }

    virtual bool pressButton(Base::Vector2D onSketchPos)
    {
        return true;
    }

    virtual bool releaseButton(Base::Vector2D onSketchPos)
    {
        sketchgui->purgeHandler(); // no code after this line, Handler get deleted in ViewProvider
        return true;
    }

    virtual bool onSelectionChanged(const Gui::SelectionChanges& msg)
    {
        if (msg.Type == Gui::SelectionChanges::AddSelection) {
            std::string subName(msg.pSubName);
            if ((subName.size() > 4 && subName.substr(0,4) == "Edge") ||
                (subName.size() > 6 && subName.substr(0,6) == "Vertex")) {
                try {
                    Gui::Command::openCommand("Add external geometry");
                    Gui::Command::doCommand(Gui::Command::Doc,"App.ActiveDocument.%s.addExternal(\"%s\",\"%s\")",
                              sketchgui->getObject()->getNameInDocument(),
                              msg.pObjectName, msg.pSubName);
                    Gui::Command::commitCommand();
                    Gui::Command::updateActive();
                    Gui::Selection().clearSelection();
                    sketchgui->purgeHandler(); // no code after this line, Handler get deleted in ViewProvider
                }
                catch (const Base::Exception& e) {
                    Base::Console().Error("%s\n", e.what());
                }
                return true;
            }
        }
        return false;
    }
};

DEF_STD_CMD_A(CmdSketcherExternal);

CmdSketcherExternal::CmdSketcherExternal()
  : Command("Sketcher_External")
{
    sAppModule      = "Sketcher";
    sGroup          = QT_TR_NOOP("Sketcher");
    sMenuText       = QT_TR_NOOP("External geometry");
    sToolTipText    = QT_TR_NOOP("Create an edge linked to an external geometry");
    sWhatsThis      = sToolTipText;
    sStatusTip      = sToolTipText;
    sPixmap         = "Sketcher_External";
    sAccel          = "E";
    eType           = ForEdit;
}

void CmdSketcherExternal::activated(int iMsg)
{
    ActivateHandler(getActiveGuiDocument(), new DrawSketchHandlerExternal());
}

bool CmdSketcherExternal::isActive(void)
{
    return isCreateGeoActive(getActiveGuiDocument());
}

/* Create Slot =======================================================*/

/* XPM */
static const char *cursor_creatslot[]={
"32 32 3 1",
"+ c white",
"# c red",
". c None",
"......+.........................",
"......+.........................",
"......+.........................",
"......+.........................",
"......+.........................",
"................................",
"+++++...+++++...................",
"................................",
"......+.........................",
"......+.........................",
"......+.........................",
"......+.........................",
"......+.........................",
"................................",
"................................",
"..........................###...",
"........###################.##..",
".......#..................###.#.",
"......#........................#",
".....#.........................#",
"....#.....###..................#",
"....#.....#.#..................#",
".....#....###.................#.",
"......#.......................#.",
".......#.....................#..",
"........#####################...",
"................................",
"................................",
"................................",
"................................",
"................................",
"................................"};

class DrawSketchHandlerSlot: public DrawSketchHandler
{
public:
    DrawSketchHandlerSlot():Mode(STATUS_SEEK_First),EditCurve(36){}
    virtual ~DrawSketchHandlerSlot(){}
    /// mode table
    enum BoxMode {
        STATUS_SEEK_First,      /**< enum value ----. */
        STATUS_SEEK_Second,     /**< enum value ----. */
        STATUS_End
    };

    virtual void activated(ViewProviderSketch *sketchgui)
    {
        setCursor(QPixmap(cursor_creatslot),7,7);
    }

    virtual void mouseMove(Base::Vector2D onSketchPos)
    {

        if (Mode==STATUS_SEEK_First) {
            setPositionText(onSketchPos);
            if (seekAutoConstraint(sugConstr1, onSketchPos, Base::Vector2D(0.f,0.f))) {
                renderSuggestConstraintsCursor(sugConstr1);
                return;
            }
        }
        else if (Mode==STATUS_SEEK_Second) {
            float dx = onSketchPos.fX - StartPos.fX;
            float dy = onSketchPos.fY - StartPos.fY;

            lx=0;ly=0;a=0;
            if(fabs(dx) > fabs(dy)){
                lx = dx;
                r = dy;
                rev = dx/fabs(dx);
            }else{
                ly = dy;
                r = dx;
                a = 8;
                rev = dy/fabs(dy);
            }

            for (int i=0; i < 17; i++) {
                double angle = (i+a)*M_PI/16.0;
                double rx = -fabs(r)* rev * sin(angle) ;
                double ry = fabs(r) * rev *cos(angle) ;
                EditCurve[i] = Base::Vector2D(StartPos.fX + rx, StartPos.fY + ry);
                EditCurve[18+i] = Base::Vector2D(StartPos.fX - rx+lx, StartPos.fY - ry+ly);
            }
            EditCurve[17] = EditCurve[16] + Base::Vector2D(lx,ly);
            EditCurve[35] = EditCurve[0] ;
            //EditCurve[34] = EditCurve[0];

            // Display radius for user
            float radius = (onSketchPos - EditCurve[0]).Length();

            SbString text;
            text.sprintf(" (%.1fR %.1fL)", r,lx);
            setPositionText(onSketchPos, text);

            sketchgui->drawEdit(EditCurve);
            if (seekAutoConstraint(sugConstr2, onSketchPos, Base::Vector2D(0.f,0.f),
                                   AutoConstraint::CURVE)) {
                renderSuggestConstraintsCursor(sugConstr2);
                return;
            }
        }
        applyCursor();
    }

    virtual bool pressButton(Base::Vector2D onSketchPos)
    {
        if (Mode==STATUS_SEEK_First){
            StartPos = onSketchPos;
            Mode = STATUS_SEEK_Second;
        }
        else {
            Mode = STATUS_End;
        }
        return true;
    }

    virtual bool releaseButton(Base::Vector2D onSketchPos)
    {
        if (Mode==STATUS_End){
            unsetCursor();
            resetPositionText();
            Gui::Command::openCommand("Add slot");
            int firstCurve = getHighestCurveIndex() + 1;
            // add the geometry to the sketch
            double start, end;
            if(fabs(lx)>fabs(ly)){
                start = M_PI/2;
                end = -M_PI/2;
            }else{
                start = 0;
                end = M_PI;
            }
            if(ly>0 || lx <0){
                double temp = start;
                start = end;
                end = temp;
            }

            try {
                Gui::Command::doCommand(Gui::Command::Doc,"App.ActiveDocument.%s.addGeometry(Part.ArcOfCircle(Part.Circle(App.Vector(%f,%f,0),App.Vector(0,0,1),%f),%f,%f))",
                          sketchgui->getObject()->getNameInDocument(),
                          StartPos.fX,StartPos.fY,  // center of the  arc
                          fabs(r),                  // radius
                          start,end                 // start and end angle
                          );
                Gui::Command::doCommand(Gui::Command::Doc,"App.ActiveDocument.%s.addGeometry(Part.ArcOfCircle(Part.Circle(App.Vector(%f,%f,0),App.Vector(0,0,1),%f),%f,%f))",
                          sketchgui->getObject()->getNameInDocument(),
                          StartPos.fX+lx,StartPos.fY+ly,    // center of the  arc
                          fabs(r),                          // radius
                          end,start                         // start and end angle
                          );

                Gui::Command::doCommand(Gui::Command::Doc,"App.ActiveDocument.%s.addGeometry(Part.Line(App.Vector(%f,%f,0),App.Vector(%f,%f,0)))",
                          sketchgui->getObject()->getNameInDocument(),
                          EditCurve[16].fX,EditCurve[16].fY,EditCurve[17].fX,EditCurve[17].fY);
                Gui::Command::doCommand(Gui::Command::Doc,"App.ActiveDocument.%s.addGeometry(Part.Line(App.Vector(%f,%f,0),App.Vector(%f,%f,0)))",
                          sketchgui->getObject()->getNameInDocument(),
                          EditCurve[0].fX,EditCurve[0].fY,EditCurve[34].fX,EditCurve[34].fY);
                //// add the tnagent constraints
                Gui::Command::doCommand(Gui::Command::Doc,"App.ActiveDocument.%s.addConstraint(Sketcher.Constraint('Tangent',%i,%i)) "
                         ,sketchgui->getObject()->getNameInDocument()
                         ,firstCurve,firstCurve+2);
                Gui::Command::doCommand(Gui::Command::Doc,"App.ActiveDocument.%s.addConstraint(Sketcher.Constraint('Tangent',%i,%i)) "
                         ,sketchgui->getObject()->getNameInDocument()
                         ,firstCurve,firstCurve+3);
                Gui::Command::doCommand(Gui::Command::Doc,"App.ActiveDocument.%s.addConstraint(Sketcher.Constraint('Tangent',%i,%i)) "
                         ,sketchgui->getObject()->getNameInDocument()
                         ,firstCurve+1,firstCurve+2);
                Gui::Command::doCommand(Gui::Command::Doc,"App.ActiveDocument.%s.addConstraint(Sketcher.Constraint('Tangent',%i,%i)) "
                         ,sketchgui->getObject()->getNameInDocument()
                         ,firstCurve+1,firstCurve+3);
                // add the four coincidents to ty them together
                Gui::Command::doCommand(Gui::Command::Doc,"App.ActiveDocument.%s.addConstraint(Sketcher.Constraint('Coincident',%i,1,%i,1)) "
                         ,sketchgui->getObject()->getNameInDocument()
                         ,firstCurve,firstCurve+3);
                Gui::Command::doCommand(Gui::Command::Doc,"App.ActiveDocument.%s.addConstraint(Sketcher.Constraint('Coincident',%i,2,%i,1)) "
                         ,sketchgui->getObject()->getNameInDocument()
                         ,firstCurve,firstCurve+2);
                Gui::Command::doCommand(Gui::Command::Doc,"App.ActiveDocument.%s.addConstraint(Sketcher.Constraint('Coincident',%i,2,%i,1)) "
                         ,sketchgui->getObject()->getNameInDocument()
                         ,firstCurve+2,firstCurve+1);
                Gui::Command::doCommand(Gui::Command::Doc,"App.ActiveDocument.%s.addConstraint(Sketcher.Constraint('Coincident',%i,2,%i,2)) "
                         ,sketchgui->getObject()->getNameInDocument()
                         ,firstCurve+3,firstCurve+1);
                //// add the either horizontal or vertical constraints
                if(fabs(lx)>fabs(ly))
                    Gui::Command::doCommand(Gui::Command::Doc,"App.ActiveDocument.%s.addConstraint(Sketcher.Constraint('Horizontal',%i)) "
                             ,sketchgui->getObject()->getNameInDocument()
                             ,firstCurve+2);
                else
                    Gui::Command::doCommand(Gui::Command::Doc,"App.ActiveDocument.%s.addConstraint(Sketcher.Constraint('Vertical',%i)) "
                             ,sketchgui->getObject()->getNameInDocument()
                             ,firstCurve+2);
                // make the two arcs equal
                Gui::Command::doCommand(Gui::Command::Doc,"App.ActiveDocument.%s.addConstraint(Sketcher.Constraint('Equal',%i,%i)) "
                         ,sketchgui->getObject()->getNameInDocument()
                         ,firstCurve,firstCurve+1);

                Gui::Command::commitCommand();
                Gui::Command::updateActive();

                // add auto constraints at the start of the first side
                if (sugConstr1.size() > 0) {
                    createAutoConstraints(sugConstr1, getHighestCurveIndex() - 3 , Sketcher::mid);
                    sugConstr1.clear();
                }

                // add auto constraints at the end of the second side
                if (sugConstr2.size() > 0) {
                    createAutoConstraints(sugConstr2, getHighestCurveIndex() - 2, Sketcher::end);
                    sugConstr2.clear();
                }
            }
            catch (const Base::Exception& e) {
                Base::Console().Error("%s\n", e.what());
                Gui::Command::abortCommand();
                Gui::Command::updateActive();
            }

            EditCurve.clear();
            sketchgui->drawEdit(EditCurve);
            sketchgui->purgeHandler(); // no code after this line, Handler get deleted in ViewProvider
        }
        return true;
    }
protected:
    BoxMode Mode;
    Base::Vector2D StartPos;
    double lx,ly,r,a,rev;
    std::vector<Base::Vector2D> EditCurve;
    std::vector<AutoConstraint> sugConstr1, sugConstr2;
};

DEF_STD_CMD_A(CmdSketcherCreateSlot);

CmdSketcherCreateSlot::CmdSketcherCreateSlot()
  : Command("Sketcher_CreateSlot")
{
    sAppModule      = "Sketcher";
    sGroup          = QT_TR_NOOP("Sketcher");
    sMenuText       = QT_TR_NOOP("Create slot");
    sToolTipText    = QT_TR_NOOP("Create a slot in the sketch");
    sWhatsThis      = sToolTipText;
    sStatusTip      = sToolTipText;
    sPixmap         = "Sketcher_CreateSlot";
    sAccel          = "";
    eType           = ForEdit;
}

void CmdSketcherCreateSlot::activated(int iMsg)
{
    ActivateHandler(getActiveGuiDocument(),new DrawSketchHandlerSlot() );
}

bool CmdSketcherCreateSlot::isActive(void)
{
    return isCreateGeoActive(getActiveGuiDocument());
}

void CreateSketcherCommandsCreateGeo(void)
{
    Gui::CommandManager &rcCmdMgr = Gui::Application::Instance->commandManager();

    rcCmdMgr.addCommand(new CmdSketcherCreatePoint());
    rcCmdMgr.addCommand(new CmdSketcherCreateArc());
    rcCmdMgr.addCommand(new CmdSketcherCreate3PointArc());
    rcCmdMgr.addCommand(new CmdSketcherCompCreateArc());
    rcCmdMgr.addCommand(new CmdSketcherCreateCircle());
    rcCmdMgr.addCommand(new CmdSketcherCreate3PointCircle());
    rcCmdMgr.addCommand(new CmdSketcherCompCreateCircle());
    rcCmdMgr.addCommand(new CmdSketcherCreateLine());
    rcCmdMgr.addCommand(new CmdSketcherCreatePolyline());
    rcCmdMgr.addCommand(new CmdSketcherCreateRectangle());
    rcCmdMgr.addCommand(new CmdSketcherCreateSlot());
    rcCmdMgr.addCommand(new CmdSketcherCreateFillet());
    //rcCmdMgr.addCommand(new CmdSketcherCreateText());
    //rcCmdMgr.addCommand(new CmdSketcherCreateDraftLine());
    rcCmdMgr.addCommand(new CmdSketcherTrimming());
    rcCmdMgr.addCommand(new CmdSketcherExternal());
}
