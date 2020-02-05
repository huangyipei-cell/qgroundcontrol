/****************************************************************************
 *
 *   (c) 2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "MapGridMGRS.h"
#include "QGCGeo.h"
#include <QJsonObject>
#include <QJsonArray>
#include <QLoggingCategory>
#include <math.h>

//=============================================================================
MGRSZone::MGRSZone(QString _label)
{
    label = _label;

    if (!convertMGRSToGeo(label + "0000000000", bottomLeft)) {
        valid = false;
    }

    if (!convertMGRSToGeo(label + "2000050000", searchPos)) {
        valid = false;
    }

    if (!convertMGRSToGeo(label + "0000099999", topLeft)) {
        valid = false;
    }

    if (!convertMGRSToGeo(label + "9999900000", bottomRight)) {
        valid = false;
    }

    if (!convertMGRSToGeo(label + "9999999999", topRight)) {
        valid = false;
    }

    QString l2l = MapGridMGRS::level2Label(label);

    // top Right overlap
    if (MapGridMGRS::level2Label(convertGeoToMGRS(topRight)) != l2l) {
        _fixEdge(l2l, 100000, -1, "%1 99999", topRight);
    }

    // top Left overlap
    if (MapGridMGRS::level2Label(convertGeoToMGRS(topLeft)) != l2l) {
        leftOverlap = true;
        _fixEdge(l2l, 0, 1, "%1 99999", topLeft);
    }

    // Bottom Right overlap
    if (MapGridMGRS::level2Label(convertGeoToMGRS(bottomRight)) != l2l) {
        rightOverlap = true;
        _fixEdge(l2l, 100000, -1, "%1 00000", bottomRight);
    }

    // Bottom Left overlap
    if (MapGridMGRS::level2Label(convertGeoToMGRS(bottomLeft)) != l2l) {
        leftOverlap = true;
        _fixEdge(l2l, 0, 1, "%1 00000", bottomLeft);
    }

    if (!convertMGRSToGeo(label + "5000050000", labelPos)) {
        rightOverlap = true;
        valid = false;
    }
    labelPos.setLongitude((topLeft.longitude() + topRight.longitude() + bottomLeft.longitude() + bottomRight.longitude()) / 4);

    QString searchLabel;

    if (!convertMGRSToGeo(label + "9999950000", rightSearchPos)) {
        valid = false;
    }
    rightSearchPos = rightSearchPos.atDistanceAndAzimuth(100, 90);

    if (!convertMGRSToGeo(label + "0000050000", leftSearchPos)) {
        valid = false;
    }
    leftSearchPos = leftSearchPos.atDistanceAndAzimuth(100, 270);

    if (!convertMGRSToGeo(label + "5000000000", bottomSearchPos)) {
        valid = false;
    }
    bottomSearchPos = bottomSearchPos.atDistanceAndAzimuth(100, 180);

    if (!convertMGRSToGeo(label + "5000099999", topSearchPos)) {
        valid = false;
    }
    topSearchPos = topSearchPos.atDistanceAndAzimuth(100, 0);
}

//-----------------------------------------------------------------------------
void
MGRSZone::_fixEdge(QString l2l, int start, int dir, QString format, QGeoCoordinate& edgeToFix)
{
    QString mgrsC;
    QGeoCoordinate c;
    int coord = start;

    for (double step = 10000; step >= 1; step /= 10) {
        do {
            coord += step * dir;
            mgrsC = label + " " + QString(format).arg(coord, 5, 10, QChar('0'));
            if (!convertMGRSToGeo(mgrsC, c)) {
                break;
            }
        } while (MapGridMGRS::level2Label(convertGeoToMGRS(c)) != l2l);
        coord -= step * dir;
    }
    convertMGRSToGeo(mgrsC, edgeToFix);
}

//=============================================================================
bool
MapGridMGRS::lineIntersectsLine(const QGeoCoordinate& l1p1, const QGeoCoordinate& l1p2, const QGeoCoordinate& l2p1, const QGeoCoordinate& l2p2)
{
    double q = (l1p1.latitude() - l2p1.latitude()) * (l2p2.longitude() - l2p1.longitude()) - (l1p1.longitude() - l2p1.longitude()) * (l2p2.latitude() - l2p1.latitude());
    double d = (l1p2.longitude() - l1p1.longitude()) * (l2p2.latitude() - l2p1.latitude()) - (l1p2.latitude() - l1p1.latitude()) * (l2p2.longitude() - l2p1.longitude());

    if (d == 0) {
        return false;
    }
    double r = q / d;
    q = (l1p1.latitude() - l2p1.latitude()) * (l1p2.longitude() - l1p1.longitude()) - (l1p1.longitude() - l2p1.longitude()) * (l1p2.latitude() - l1p1.latitude());
    double s = q / d;

    if (r < 0 || r > 1 || s < 0 || s > 1) {
        return false;
    }

    return true;
}

//-----------------------------------------------------------------------------
bool
MapGridMGRS::lineIntersectsRect(const QGeoCoordinate& p1, const QGeoCoordinate& p2, const QGeoRectangle& r)
{
    return
        lineIntersectsLine(p1, p2, r.topRight(), r.topLeft()) ||
        lineIntersectsLine(p1, p2, r.topLeft(), r.bottomLeft()) ||
        lineIntersectsLine(p1, p2, r.bottomLeft(), r.bottomRight()) ||
        lineIntersectsLine(p1, p2, r.bottomRight(), r.topRight()) ||
        (r.contains(p1) && r.contains(p2));
}

//-----------------------------------------------------------------------------
void
MapGridMGRS::geometryChanged(double zoomLevel, const QGeoCoordinate& topLeft, const QGeoCoordinate& bottomRight)
{
    if (topLeft.isValid() && bottomRight.isValid()) {
        _zoomLevel = zoomLevel;
        //qCritical() << "Zoom: " << zoomLevel << "TL: " << topLeft << " ; " << bottomRight;

        double latDiff = (topLeft.latitude() - bottomRight.latitude()) / 3;
        double topLat = topLeft.latitude() + latDiff;
        if (topLat > 84) {
            topLat = 84;
        }
        double bottomLat = bottomRight.latitude() - latDiff;
        if (bottomLat < -80) {
            bottomLat = -80;
        }
        double lngDiff = (bottomRight.longitude() - topLeft.longitude()) / 3;
        double leftLng = topLeft.longitude() - lngDiff;
        if (leftLng < -180) {
            leftLng = -180;
        }
        double rightLng = bottomRight.longitude() + lngDiff;
        if (rightLng > 180) {
            rightLng = 180;
        }
        _currentMGRSRect.setTopLeft(QGeoCoordinate(topLat, leftLng));
        _currentMGRSRect.setBottomRight(QGeoCoordinate(bottomLat, rightLng));

        double centerLat = (topLeft.latitude() + bottomRight.latitude()) / 2;
        QGeoCoordinate centerLeft = QGeoCoordinate(centerLat, topLeft.longitude());
        QGeoCoordinate centerRight = QGeoCoordinate(centerLat, bottomRight.longitude());
        _minDistanceBetweenLines = centerLeft.distanceTo(centerRight) / maxNumberOfLinesOnScreen;
    } else if (_zoomLevel < 3) {
        emit updateValues(QVariant());
        return;
    }

    _clear();
    _addLevel1Lines();
    _addLevel1Labels();

    QJsonArray lines;
    _addLines(lines, _level1Paths, level1LineBackgroundColor, level1LineBackgroundWidth);
    _addLines(lines, _level1Paths, level1LineForegroundColor, level1LineForgroundWidth);

    if (_zoomLevel > maxZoneZoomLevel) {
        _findZoneBoundaries(QGeoCoordinate((topLeft.latitude() + bottomRight.latitude()) / 2, (topLeft.longitude() + bottomRight.longitude()) / 2));
        _addLines(lines, _level2Paths, level2LineBackgroundColor, level2LineBackgroundWidth);
        _addLines(lines, _level2Paths, level2LineForegroundColor, level2LineForgroundWidth);
        _addLines(lines, _level3Paths, level3LineBackgroundColor, level3LineBackgroundWidth);
        _addLines(lines, _level3Paths, level3LineForegroundColor, level3LineForgroundWidth);
    }

    QJsonArray labels;
    _addLabels(labels);

    QJsonObject values;
    values.insert(QStringLiteral("lines"), lines);
    values.insert(QStringLiteral("labels"), labels);

    emit updateValues(QVariant(values));
}

//-----------------------------------------------------------------------------
void
MapGridMGRS::_addLevel1Lines()
{
    if (_level1Hlines.empty()) {
        for (int lat = -80; lat <= 84; lat += (lat < 70) ? 8 : 12) {
            QGeoPath path1;
            path1.addCoordinate(QGeoCoordinate(lat, 0));
            path1.addCoordinate(QGeoCoordinate(lat, 179));
            _level1Hlines.push_back(path1);
            QGeoPath path2;
            path2.addCoordinate(QGeoCoordinate(lat, -160));
            path2.addCoordinate(QGeoCoordinate(lat, 0));
            _level1Hlines.push_back(path2);
            QGeoPath path3;
            path2.addCoordinate(QGeoCoordinate(lat, -180));
            path2.addCoordinate(QGeoCoordinate(lat, -160));
            _level1Hlines.push_back(path3);
        }
    }
    if (_level1Vlines.empty()) {
        for (int lng = -180; lng <= 180; lng += 6) {
            QGeoPath path;
            if (lng == 6) {
                // Norway anomaly
                path.addCoordinate(QGeoCoordinate(-80, lng));
                path.addCoordinate(QGeoCoordinate(56, lng));
                QGeoPath path2;
                path2.addCoordinate(QGeoCoordinate(56, lng - 3));
                path2.addCoordinate(QGeoCoordinate(64, lng - 3));
                _level1Vlines.push_back(path2);
                QGeoPath path3;
                path3.addCoordinate(QGeoCoordinate(64, lng));
                path3.addCoordinate(QGeoCoordinate(72, lng));
                _level1Vlines.push_back(path3);
            } else if (lng >= 12 && lng <= 36 ) {
                // Svalbard anomaly
                path.addCoordinate(QGeoCoordinate(-80, lng));
                path.addCoordinate(QGeoCoordinate(72, lng));
            } else {
                path.addCoordinate(QGeoCoordinate(-80, lng));
                path.addCoordinate(QGeoCoordinate(84, lng));
            }
            _level1Vlines.push_back(path);
        }
        for (int lng = 9; lng <= 33; lng += 12) {
            // Svalbard anomaly
            QGeoPath path;
            path.addCoordinate(QGeoCoordinate(72, lng));
            path.addCoordinate(QGeoCoordinate(84, lng));
            _level1Vlines.push_back(path);
        }
    }

    for (int i = 0; i < _level1Hlines.count(); i++) {
        double lat = _level1Hlines[i].coordinateAt(0).latitude();
        if (_zoomLevel < 6 || (lat > _currentMGRSRect.bottomLeft().latitude() && lat < _currentMGRSRect.topLeft().latitude())) {
            _level1Paths.push_back(_level1Hlines[i]);
        }
    }
    for (int i = 0; i < _level1Vlines.count(); i++) {
        double lng = _level1Vlines[i].coordinateAt(0).longitude();
        if (_zoomLevel < 6 || (lng > _currentMGRSRect.bottomLeft().longitude() && lng < _currentMGRSRect.bottomRight().longitude())) {
            _level1Paths.push_back(_level1Vlines[i]);
        }
    }
}

//-----------------------------------------------------------------------------
void
MapGridMGRS::_addLevel1Labels()
{
    if (_level1labels.empty()) {
        for (int lng = -180; lng <= 180; lng += 6) {
            for (int lat = -80; lat <= 84; lat += (lat < 70) ? 8 : 12) {
                if ((lat == 72 && (lng == 6 || lng == 18 || lng == 30))) {
                    continue;
                }
                QString text = level1Label(convertGeoToMGRS(QGeoCoordinate(lat, lng)));
                double llat = lat + 4;
                double llng = lng + 3;
                if (text == "31V") {
                    llng = lng + 1.75;
                } else if (text == "31X") {
                    llng = lng + 4.5;
                } else if (text == "32V") {
                    llng = lng + 2;
                } else if (text == "37X") {
                    llng = lng + 1.5;
                }

                QGeoCoordinate pos(llat, llng);
                _level1labels.push_back(MGRSLabel(text, pos, level1LabelForegroundColor, level1LabelBackgroundColor));
            }
        }
    }

    if (_zoomLevel > 4 && _zoomLevel <= maxZoneZoomLevel) {
        for (int i = 0; i < _level1labels.count(); i++) {
            if (_currentMGRSRect.contains(_level1labels[i]._pos)) {
                _mgrsLabels.push_back(_level1labels[i]);
            }
        }
    }
}

//-----------------------------------------------------------------------------
void
MapGridMGRS::_clear()
{
    _level1Paths.clear();
    _level2Paths.clear();
    _level3Paths.clear();
    _mgrsLabels.clear();

    for (auto i = _zoneMap.begin(); i != _zoneMap.end(); ++i)
        i.value()->visited = false;
}

//-----------------------------------------------------------------------------
void
MapGridMGRS::_findZoneBoundaries(const QGeoCoordinate& pos)
{
    QString mgrsPos = convertGeoToMGRS(pos);
    QString label = zoneLabel(mgrsPos);

    std::shared_ptr<MGRSZone> tile = _zoneMap.value(label);
    if (!tile) {
        tile = std::shared_ptr<MGRSZone>(new MGRSZone(label));
        _zoneMap.insert(label, tile);
        _zoneMapQueue.enqueue(label);
        if (_zoneMapQueue.count() > maxZoneMapCacheSize) {
            _zoneMap.remove(_zoneMapQueue.dequeue());
        }
    }

    if (tile->valid && !tile->visited && pos.latitude() < 84 && pos.latitude() > -80 &&
        (_currentMGRSRect.contains(tile->bottomLeft) || _currentMGRSRect.contains(tile->bottomRight) ||
         _currentMGRSRect.contains(tile->topLeft) || _currentMGRSRect.contains(tile->topRight) ||
         _currentMGRSRect.contains(pos))) {
        tile->visited = true;

        _findZoneBoundaries(tile->topSearchPos);
        _findZoneBoundaries(tile->rightSearchPos);
        _findZoneBoundaries(tile->bottomSearchPos);
        _findZoneBoundaries(tile->leftSearchPos);

        QGeoPath path;
        if (!tile->leftOverlap) {
            path.addCoordinate(tile->topLeft);
        }
        path.addCoordinate(tile->bottomLeft);
        path.addCoordinate(tile->bottomRight);

        _level2Paths.push_back(path);

        if (_zoomLevel > maxZoneZoomLevel && _currentMGRSRect.contains(tile->labelPos)) {
            _mgrsLabels.push_back(MGRSLabel(MapGridMGRS::level2Label(tile->label), tile->labelPos, level2LabelForegroundColor, level2LabelBackgroundColor));
        }

        _createLevel3Paths(tile);
    }
}
//-----------------------------------------------------------------------------
void
MapGridMGRS::_createLevel3Paths(std::shared_ptr<MGRSZone> &tile)
{
    QGeoCoordinate c1;
    QGeoCoordinate c2;
    int distanceBetweenLines = 0;
    const int factors[] = { 500, 1000, 5000, 10000, 50000 };

    QGeoCoordinate bl;
    if (!convertMGRSToGeo(tile->label + "0000000000", bl)) {
        return;
    }

    QGeoRectangle tileRect(tile->topLeft, tile->bottomRight);
    tileRect.setTopRight(tile->topRight);
    tileRect.setBottomLeft(tile->bottomLeft);

    bool overlapped = tile->leftOverlap || tile->rightOverlap;

    for (int i = 0; i <= 4; i ++) {
        if (!convertMGRSToGeo(tile->label + QString("%1").arg(factors[i], 5, 10, QChar('0')) + "00000", c1)) {
            return;
        }
        if (bl.distanceTo(c1) > _minDistanceBetweenLines) {
            distanceBetweenLines = factors[i];
            break;
        }
    }
    if (distanceBetweenLines <= 0) {
        return;
    }

    int cnt1 = 0;
    for (int i = distanceBetweenLines; i < 100000; i += distanceBetweenLines) {
        cnt1++;
        // Horizontal lines
        QString coord = QString("%1").arg(i, 5, 10, QChar('0'));
        if (!convertMGRSToGeo(tile->label + "00000" + coord, c1)) {
            return;
        }
        int added = 0;
        QGeoPath path;
        if (!overlapped || tileRect.contains(c1)) {
            path.addCoordinate(c1);
            added++;
        }
        for (int j = distanceBetweenLines; j <= 100000; j += distanceBetweenLines) {
            if (j == 100000)
                j--;
            QString coordE = QString("%1").arg(j, 5, 10, QChar('0'));
            if (!convertMGRSToGeo(tile->label + coordE + coord, c2)) {
                break;
            }
            if (lineIntersectsRect(c1, c2, _currentMGRSRect) && (!overlapped || tileRect.contains(c2))) {
                path.addCoordinate(c2);
                added++;
            } else if (added > 2) {
                break;
            }
            c1 = c2;
        }
        if (added > 1) {
            _level3Paths.push_back(path);
        }

        // Vertical lines
        if (!convertMGRSToGeo(tile->label + coord + "00000", c1)) {
            return;
        }

        added = 0;
        path.clearPath();
        if (!overlapped || tileRect.contains(c1)) {
            path.addCoordinate(c1);
            added++;
        }
        int cnt2 = 0;
        for (int j = distanceBetweenLines; j <= 100000; j += distanceBetweenLines) {
            cnt2++;
            if (j == 100000)
                j--;
            QString coordN = QString("%1").arg(j, 5, 10, QChar('0'));
            if (!convertMGRSToGeo(tile->label + coord + coordN, c2)) {
                break;
            }
            if (lineIntersectsRect(c1, c2, _currentMGRSRect) && (!overlapped || tileRect.contains(c2))) {
                path.addCoordinate(c2);
                added++;
                if (added > 1 && cnt1 % 2 == 1 && cnt2 % 2 == 1 &&_zoomLevel > maxZoneZoomLevel &&
                    !(coord == "50000" && coordN == "50000") && _currentMGRSRect.contains(c2)) {
                    QString text = level2Label(tile->label) + " " + coord.left(2) + " " + coordN.left(2);
                    _mgrsLabels.push_back(MGRSLabel(text, c2, level3LabelForegroundColor, level3LabelBackgroundColor));
                }
            } else if (added > 2) {
                break;
            }
            c1 = c2;
        }
        if (added > 1) {
            _level3Paths.push_back(path);
        }
    }
}

//-----------------------------------------------------------------------------
void
MapGridMGRS::_addLines(QJsonArray& lines, const QList<QGeoPath>& paths, const QString& color, int width)
{
    for (int i = 0; i < paths.length(); i++) {
        QJsonArray pathPts;
        for (int j = 0; j < paths[i].size(); j++) {
            QGeoCoordinate c = paths[i].coordinateAt(j);
            QJsonObject p;
            p.insert(QStringLiteral("lat"), c.latitude());
            p.insert(QStringLiteral("lng"), c.longitude());
            pathPts.push_back(p);
        }
        QJsonObject line;
        line.insert(QStringLiteral("points"), pathPts);
        line.insert(QStringLiteral("color"), color);
        line.insert(QStringLiteral("width"), width);
        lines.push_back(line);
    }
}

//-----------------------------------------------------------------------------
void
MapGridMGRS::_addLabels(QJsonArray& labels)
{
    for (int i = 0; i < _mgrsLabels.length(); i++) {
        QJsonObject label;
        label.insert(QStringLiteral("text"), _mgrsLabels[i]._label);
        label.insert(QStringLiteral("lat"), _mgrsLabels[i]._pos.latitude());
        label.insert(QStringLiteral("lng"), _mgrsLabels[i]._pos.longitude());
        label.insert(QStringLiteral("backgroundColor"), _mgrsLabels[i]._backgroundColor);
        label.insert(QStringLiteral("foregroundColor"), _mgrsLabels[i]._foregroundColor);
        labels.push_back(label);
    }
}

//-----------------------------------------------------------------------------
