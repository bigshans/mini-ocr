#include "image_view.h"

#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QPaintEvent>
#include <QPainter>
#include <QPixmap>
#include <QResizeEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

ImageView::ImageView(QWidget* parent) : QGraphicsView(parent) {
    scene_ = new QGraphicsScene(this);
    setScene(scene_);
    setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setAlignment(Qt::AlignCenter);
    setBackgroundBrush(QColor(QStringLiteral("#f4f5f7")));
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
}

void ImageView::setImage(const QImage& image) {
    scene_->clear();
    item_ = nullptr;
    if (image.isNull()) {
        clearImage();
        return;
    }

    item_ = scene_->addPixmap(QPixmap::fromImage(image));
    scene_->setSceneRect(item_->boundingRect());
    fitMode_ = true;
    if (viewport()->width() > 10 && viewport()->height() > 10) {
        resetTransform();
        fitInView(item_, Qt::KeepAspectRatio);
        notifyZoom();
    }
    update();
}

void ImageView::clearImage() {
    scene_->clear();
    item_ = nullptr;
    resetTransform();
    fitMode_ = true;
    notifyZoom();
    update();
}

void ImageView::fitToWindow() {
    if (!item_) return;
    fitMode_ = true;
    resetTransform();
    fitInView(item_, Qt::KeepAspectRatio);
    notifyZoom();
}

void ImageView::zoomIn() {
    zoomBy(1.25, QGraphicsView::AnchorViewCenter);
}

void ImageView::zoomOut() {
    zoomBy(0.8, QGraphicsView::AnchorViewCenter);
}

void ImageView::zoomBy(double factor, QGraphicsView::ViewportAnchor anchor) {
    if (!item_) return;
    const double current = transform().m11();
    const double target = std::clamp(current * factor, 0.05, 8.0);
    if (std::abs(target - current) < 1e-6) return;

    fitMode_ = false;
    setTransformationAnchor(anchor);
    scale(target / current, target / current);
    notifyZoom();
}

int ImageView::zoomPercent() const {
    if (!item_) return 0;
    return static_cast<int>(std::lround(transform().m11() * 100.0));
}

void ImageView::notifyZoom() {
    if (callback_) callback_(zoomPercent(), fitMode_);
}

void ImageView::wheelEvent(QWheelEvent* event) {
    if (!item_) {
        QGraphicsView::wheelEvent(event);
        return;
    }
    const int delta = event->angleDelta().y();
    if (delta == 0) return;

    const double factor = std::pow(1.0015, delta);
    zoomBy(factor, QGraphicsView::AnchorUnderMouse);
    event->accept();
}

void ImageView::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    if (fitMode_ && item_ && viewport()->width() > 10 && viewport()->height() > 10) {
        resetTransform();
        fitInView(item_, Qt::KeepAspectRatio);
        notifyZoom();
    }
}

void ImageView::paintEvent(QPaintEvent* event) {
    QGraphicsView::paintEvent(event);
    if (!item_) {
        QPainter painter(viewport());
        painter.setPen(QColor(QStringLiteral("#888888")));
        painter.drawText(viewport()->rect(), Qt::AlignCenter,
                         QStringLiteral("选择图片预览"));
    }
}
