#include "graphical_cfg_node.h"
#include "node.h" 
#include <QGraphicsSceneMouseEvent>
#include <QStyleOptionGraphicsItem>

GraphicalCFGNode::GraphicalCFGNode(const std::shared_ptr<CFGNode>& cfgNode, QGraphicsItem* parent)
    : QGraphicsItem(parent), m_cfgNode(cfgNode), m_color(Qt::blue) {
    setFlag(QGraphicsItem::ItemIsMovable);
    setFlag(QGraphicsItem::ItemIsSelectable);
}

QRectF GraphicalCFGNode::boundingRect() const {
    return QRectF(0, 0, NODE_WIDTH, NODE_HEIGHT);
}

void GraphicalCFGNode::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(widget);

    // Draw node rectangle
    QRectF rect = boundingRect();
    
    // Set color based on node type
    switch (m_cfgNode->getType()) {
        case CFGNode::ENTRY:
            m_color = Qt::green;
            break;
        case CFGNode::EXIT:
            m_color = Qt::red;
            break;
        case CFGNode::CONDITIONAL:
            m_color = Qt::yellow;
            break;
        case CFGNode::FUNCTION_CALL:
            m_color = Qt::cyan;
            break;
        default:
            m_color = Qt::blue;
    }

    // Highlight if selected
    if (option->state & QStyle::State_Selected) {
        painter->setPen(QPen(Qt::black, 3));
    } else {
        painter->setPen(QPen(Qt::black, 1));
    }

    painter->setBrush(m_color);
    painter->drawRect(rect);

    // Draw node content
    painter->drawText(rect, Qt::AlignCenter | Qt::TextWordWrap, 
        QString::fromStdString(m_cfgNode->getContent()));
}

void GraphicalCFGNode::setColor(const QColor& color) {
    m_color = color;
    update(); // Trigger a repaint
}

QString GraphicalCFGNode::getNodeLabel() const {
    return QString::fromStdString(m_cfgNode->getContent());
}

void GraphicalCFGNode::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    // Optional: Add custom behavior on mouse press
    QGraphicsItem::mousePressEvent(event);
}