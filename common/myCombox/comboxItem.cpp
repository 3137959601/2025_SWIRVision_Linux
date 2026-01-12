#include "comboxItem.h"

comboxItem::comboxItem(QString text, bool del)
{
    name = text;

    layout = new QHBoxLayout(this);
    btn = new QPushButton();

    layout->addStretch(0);
    layout->addWidget(btn, 1, Qt::AlignRight);
    //layout->setMargin(0);
    layout->setContentsMargins(0,0,0,0);

    //btn->setFixedSize(15, 17);
    btn->setMaximumWidth(15);
    btn->setFlat(true);
    btn->setText("×");

    //setFixedHeight(17);

    connect(btn, SIGNAL(clicked(bool)), this, SLOT(btn_clicked()));

    if (del == false) {
        btn->hide();
    }
}

comboxItem::~comboxItem()
{
    delete layout;
    delete btn;
}

void comboxItem::btn_clicked()
{
    emit del_clicked(name);
}

void comboxItem::show_del_btn()
{
    btn->show();
}

void comboxItem::hide_del_btn()
{
    btn->hide();
}

bool comboxItem::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        emit mouse_enter(name);
    }

    return false;
}
