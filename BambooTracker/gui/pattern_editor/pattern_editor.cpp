#include "pattern_editor.hpp"
#include <QPainter>
#include <QFontMetrics>
#include <QPoint>
#include <QApplication>
#include <algorithm>

#include <QDebug>

PatternEditor::PatternEditor(QWidget *parent)
	: QWidget(parent),
	  leftTrackNum_(0)
{	
	/* Font */
	headerFont_ = QApplication::font();
	headerFont_.setPointSize(10);
	rowFont_ = QFont("Monospace", 10);
	rowFont_.setStyleHint(QFont::TypeWriter);
	rowFont_.setStyleStrategy(QFont::ForceIntegerMetrics);
	// Check font size
	QFontMetrics metrics(rowFont_);
	rowFontWidth_ = metrics.width('0');
	rowFontAscend_ = metrics.ascent();
	rowFontHeight_ = metrics.height();
	rowFontLeading_ = metrics.leading();

	/* Width & height */
	widthSpace_ = rowFontWidth_ / 2;
	rowNumWidth_ = rowFontWidth_ * 2 + widthSpace_;
	toneNameWidth_ = rowFontWidth_ * 3;
	instWidth_ = rowFontWidth_ * 2;
	volWidth_ = rowFontWidth_ * 2;
	effWidth_ = rowFontWidth_ * 3;
	trackWidth_ = toneNameWidth_ + instWidth_ + volWidth_ + effWidth_ + rowFontWidth_ * 4;
	headerHeight_ = rowFontHeight_ * 2;

	/* Color */
	defTextColor_ = QColor::fromRgb(180, 180, 180);
	defRowColor_ = QColor::fromRgb(0, 0, 40);
	mkRowColor_ = QColor::fromRgb(40, 40, 80);
	curTextColor_ = QColor::fromRgb(255, 255, 255);
	curRowColor_ = QColor::fromRgb(110, 90, 140);
	curRowColorEditable_ = QColor::fromRgb(140, 90, 110);
	curCellColor_ = QColor::fromRgb(255, 255, 255, 127);
	selTextColor_ = defTextColor_;
	selCellColor_ = QColor::fromRgb(100, 100, 200);
	defRowNumColor_ = QColor::fromRgb(255, 200, 180);
	mkRowNumColor_ = QColor::fromRgb(255, 140, 160);
	headerTextColor_ = QColor::fromRgb(240, 240, 200);
	headerRowColor_ = QColor::fromRgb(60, 60, 60);
	borderColor_ = QColor::fromRgb(120, 120, 120);


	initDisplay();

	setAttribute(Qt::WA_Hover);
}

void PatternEditor::initDisplay()
{
	/* Pixmap */
	pixmap_ = std::make_unique<QPixmap>(geometry().size());
}

void PatternEditor::setCore(std::shared_ptr<BambooTracker> core)
{
	bt_ = core;
	modStyle_ = bt_->getModuleStyle();
	columnsWidthFromLeftToEnd_ = calculateColumnsWidthWithRowNum(0, modStyle_.trackAttribs.size() - 1);
}

void PatternEditor::drawPattern(const QRect &rect)
{
	int maxWidth = std::min(geometry().width(), columnsWidthFromLeftToEnd_);

	pixmap_->fill(Qt::black);
	drawRows(maxWidth);
	drawHeaders(maxWidth);
	drawBorders(maxWidth);
	if (!hasFocus()) drawShadow();

	QPainter painter(this);
	painter.drawPixmap(rect, *pixmap_.get());
}

void PatternEditor::drawRows(int maxWidth)
{
	QPainter painter(pixmap_.get());
	painter.setFont(rowFont_);

	int x, trackNum;

	int curRowNum = 32;	// dummy set
	int mkCnt = 8;

	/* Current row */
	// Fill row
	painter.fillRect(0, curRowY_, maxWidth, rowFontHeight_,
					 (bt_->isJamMode())? curRowColor_ : curRowColorEditable_);
	// Row number
	painter.setPen((curRowNum % mkCnt)? defRowNumColor_ : mkRowNumColor_);
	painter.drawText(1, curRowBaselineY_, QString("%1").arg(curRowNum, 2, 16, QChar('0')).toUpper());
	// Step data
	painter.setPen(curTextColor_);
	for (x = rowNumWidth_ + widthSpace_, trackNum = leftTrackNum_; x < maxWidth; ) {
		int offset = x;
		painter.drawText(offset, curRowBaselineY_, "---");
		offset += toneNameWidth_ +  rowFontWidth_;
		painter.drawText(offset, curRowBaselineY_, "--");
		offset += instWidth_ +  rowFontWidth_;
		painter.drawText(offset, curRowBaselineY_, "--");
		offset += volWidth_ +  rowFontWidth_;
		painter.drawText(offset, curRowBaselineY_, "---");

		switch (modStyle_.trackAttribs[trackNum].source) {
		case SoundSource::FM:
		case SoundSource::PSG:
			x += trackWidth_;
			break;
		}
		++trackNum;
	}

	int rowNum;
	int rowY, baseY;

	/* Previous rows */
	for (rowY = curRowY_ - rowFontHeight_, baseY = curRowBaselineY_ - rowFontHeight_, rowNum = curRowNum - 1;
		 rowY >= headerHeight_ - rowFontHeight_;
		 rowY -= rowFontHeight_, baseY -= rowFontHeight_, --rowNum) {
		// Fill row
		painter.fillRect(0, rowY, maxWidth, rowFontHeight_, (rowNum % mkCnt)? defRowColor_ : mkRowColor_);
		// Row number
		painter.setPen((rowNum % mkCnt)? defRowNumColor_ : mkRowNumColor_);
		painter.drawText(1, baseY, QString("%1").arg(rowNum, 2, 16, QChar('0')).toUpper());
		painter.setPen(defTextColor_);
		for (x = rowNumWidth_ + widthSpace_, trackNum = leftTrackNum_; x < maxWidth; ) {
			int offset = x;
			painter.drawText(offset, baseY, "---");
			offset += toneNameWidth_ +  rowFontWidth_;
			painter.drawText(offset, baseY, "--");
			offset += instWidth_ +  rowFontWidth_;
			painter.drawText(offset, baseY, "--");
			offset += volWidth_ +  rowFontWidth_;
			painter.drawText(offset, baseY, "---");

			switch (modStyle_.trackAttribs[trackNum].source) {
			case SoundSource::FM:
			case SoundSource::PSG:
				x += trackWidth_;
				break;
			}
			++trackNum;
		}
	}

	/* Next rows */
	for (rowY = curRowY_ + rowFontHeight_, baseY = curRowBaselineY_ + rowFontHeight_, rowNum = curRowNum + 1;
		 rowY <= geometry().height();
		 rowY += rowFontHeight_, baseY += rowFontHeight_, ++rowNum) {
		// Fill row
		painter.fillRect(0, rowY, maxWidth, rowFontHeight_, (rowNum % mkCnt)? defRowColor_ : mkRowColor_);
		// Row number
		painter.setPen((rowNum % mkCnt)? defRowNumColor_ : mkRowNumColor_);
		painter.drawText(1, baseY, QString("%1").arg(rowNum, 2, 16, QChar('0')).toUpper());
		painter.setPen(defTextColor_);
		for (x = rowNumWidth_ + widthSpace_, trackNum = leftTrackNum_; x < maxWidth; ) {
			int offset = x;
			painter.drawText(offset, baseY, "---");
			offset += toneNameWidth_ +  rowFontWidth_;
			painter.drawText(offset, baseY, "--");
			offset += instWidth_ +  rowFontWidth_;
			painter.drawText(offset, baseY, "--");
			offset += volWidth_ +  rowFontWidth_;
			painter.drawText(offset, baseY, "---");

			switch (modStyle_.trackAttribs[trackNum].source) {
			case SoundSource::FM:
			case SoundSource::PSG:
				x += trackWidth_;
				break;
			}
			++trackNum;
		}
	}
}

void PatternEditor::drawHeaders(int maxWidth)
{
	QPainter painter(pixmap_.get());
	painter.setFont(headerFont_);

	painter.fillRect(0, 0, geometry().width(), headerHeight_, headerRowColor_);
	painter.setPen(headerTextColor_);
	int x, trackNum;
	for (x = rowNumWidth_ + widthSpace_, trackNum = leftTrackNum_; x < maxWidth; ) {
		QString str;
		switch (modStyle_.trackAttribs[trackNum].source) {
		case SoundSource::FM:	str = " FM";	break;
		case SoundSource::PSG:	str = " PSG";	break;
		}
		painter.drawText(x,
						 rowFontLeading_ + rowFontAscend_,
						 str + QString::number(modStyle_.trackAttribs[trackNum].channelInSource + 1));

		switch (modStyle_.trackAttribs[trackNum].source) {
		case SoundSource::FM:
		case SoundSource::PSG:
			x += trackWidth_;
			break;
		}
		++trackNum;
	}
}

void PatternEditor::drawBorders(int maxWidth)
{
	QPainter painter(pixmap_.get());

	painter.drawLine(0, headerHeight_, geometry().width(), headerHeight_);
	painter.drawLine(rowNumWidth_, 0, rowNumWidth_, geometry().height());
	int x, trackNum;
	for (x = rowNumWidth_ + trackWidth_, trackNum = leftTrackNum_; x <= maxWidth; ) {
		painter.drawLine(x, 0, x, geometry().height());

		switch (modStyle_.trackAttribs[trackNum].source) {
		case SoundSource::FM:
		case SoundSource::PSG:
			x += trackWidth_;
			break;
		}
		++trackNum;
	}
}

void PatternEditor::drawShadow()
{
	QPainter painter(pixmap_.get());
	painter.fillRect(0, 0, geometry().width(), geometry().height(), QColor::fromRgb(0, 0, 0, 47));
}

int PatternEditor::calculateColumnsWidthWithRowNum(int begin, int end)
{
	int width = rowNumWidth_;
	for (int i = begin; i <= end; ++i) {
		switch (modStyle_.trackAttribs.at(i).source) {
		case SoundSource::FM:
		case SoundSource::PSG:
			width +=  trackWidth_;
			break;
		}
	}
	return width;
}

void PatternEditor::changeEditable()
{
	update();
}

/********** Events **********/
bool PatternEditor::event(QEvent *event)
{
	switch (event->type()) {
	case QEvent::HoverMove:
		mouseHoverd(dynamic_cast<QHoverEvent*>(event));
		return true;
	default:
		return QWidget::event(event);
	}
}

void PatternEditor::paintEvent(QPaintEvent *event)
{
	if (bt_ != nullptr) drawPattern(event->rect());
}

void PatternEditor::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);

	// Recalculate center row position
	curRowBaselineY_ = (geometry().height() - headerHeight_) / 2 + headerHeight_;
	curRowY_ = curRowBaselineY_ - (rowFontAscend_ + rowFontLeading_ / 2);

	initDisplay();
}

void PatternEditor::mousePressEvent(QMouseEvent *event)
{
	setFocus();
}

void PatternEditor::mouseHoverd(QHoverEvent *event)
{
	QPoint pos = event->pos();
	int rowNum = 0;
	int colNum = 0;

	// Detect row
	if (pos.y() <= headerHeight_) {
		// Track header
		rowNum = -1;
	}
	else {
		int curRow = 32;	// Dummy

		int tmp = (geometry().height() - curRowY_) / rowFontHeight_;
		int num = curRow + tmp;
		int y = curRowY_ + rowFontHeight_ * tmp;
		for (; ; --num, y -= rowFontHeight_) {
			if (y <= pos.y()) break;
		}
		rowNum = num;
	}

	// Detect column
	if (pos.x() <= rowNumWidth_) {
		// Row number
		colNum = -1;
	}
	else {
		// TODO
	}
}