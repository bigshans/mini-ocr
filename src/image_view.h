#pragma once

#include <QGraphicsView>
#include <QImage>

#include <functional>

class QGraphicsScene;
class QGraphicsPixmapItem;
class QPaintEvent;
class QResizeEvent;
class QWheelEvent;

class ImageView : public QGraphicsView {
public:
    explicit ImageView(QWidget* parent = nullptr);

    void setImage(const QImage& image);
    void clearImage();
    void zoomIn();
    void zoomOut();
    void fitToWindow();
    int zoomPercent() const;
    bool isFitMode() const { return fitMode_; }

    void setZoomChangedCallback(std::function<void(int, bool)> callback) {
        callback_ = std::move(callback);
    }

protected:
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void zoomBy(double factor, QGraphicsView::ViewportAnchor anchor);
    void notifyZoom();

    QGraphicsScene* scene_ = nullptr;
    QGraphicsPixmapItem* item_ = nullptr;
    bool fitMode_ = true;
    std::function<void(int, bool)> callback_;
};
