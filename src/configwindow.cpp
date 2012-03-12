/*
 * Qt-ponies - ponies on the desktop
 * Copyright (C) 2012 mysha
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <QDir>
#include <QFileDialog>
#include <QDateTime>
#include <QFile>
#include <QTextStream>

#include <iostream>
#include <algorithm>
#include <cmath>
#include <set>

#include "configwindow.h"
#include "ui_configwindow.h"

// TODO: configuration:
//       monitors (on witch to run, etc)
//       debug messages
//
//	avoidance areas (vincity of the mouse cursor for example)
//FIXME: speech enabled toggle toggles sound group visibility

const std::unordered_map<QString, const QVariant> ConfigWindow::config_defaults {
    {"general/always-on-top",        true                },
    {"general/bypass-wm",            false               },
    {"general/pony-directory",       "./desktop-ponies"  },
    {"general/interactions-enabled", true                },
    {"speech/enabled",               true                },
    {"speech/duration",              2000                },
    {"sound/enabled",                false               }
};

ConfigWindow::ConfigWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::ConfigWindow)
{
    signal_mapper = new QSignalMapper();

    ui->setupUi(this);
#ifndef Q_WS_X11
    // Do not show X11 specific options on other platforms
    ui->label_bypass_wm->setVisible(false);
    ui->x11_bypass_wm->setVisible(false);
#endif

    // Setup tray icon and menu
    tray_icon.setIcon(QIcon(":/icons/res/tray_icon.png"));

    tray_menu.addAction("Open configuration",this,SLOT(show()));
    tray_menu.addAction("Close application",QCoreApplication::instance(),SLOT(quit()));

    connect(&tray_icon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)), this, SLOT(toggle_window(QSystemTrayIcon::ActivationReason)));
    tray_icon.setContextMenu(&tray_menu);
    tray_icon.show();

    // Setup the toolbar buttons
    action_group = new QActionGroup(ui->toolBar);
    action_addponies = new QAction(QIcon(":/icons/res/add_icon.png"), "Add ponies", action_group);
    action_addponies->setCheckable(true);
    action_addponies->setChecked(true);
    action_activeponies = new QAction(QIcon(":/icons/res/active_icon.png"), "Active ponies", action_group);
    action_activeponies->setCheckable(true);
    action_configuration = new QAction(QIcon(":/icons/res/settings.png"), "Configuration", action_group);
    action_configuration->setCheckable(true);

    signal_mapper->setMapping(action_addponies,0);
    connect(action_addponies, SIGNAL(triggered()), signal_mapper, SLOT(map()));
    ui->toolBar->addAction(action_addponies);

    signal_mapper->setMapping(action_activeponies,1);
    connect(action_activeponies, SIGNAL(triggered()), signal_mapper, SLOT(map()));
    ui->toolBar->addAction(action_activeponies);

    signal_mapper->setMapping(action_configuration,2);
    connect(action_configuration, SIGNAL(triggered()), signal_mapper, SLOT(map()));
    ui->toolBar->addAction(action_configuration);

    ui->toolBar->setIconSize(QSize(100,100));

    connect(signal_mapper, SIGNAL(mapped(int)), ui->stackedWidget, SLOT(setCurrentIndex(int)));

    list_model = new QStandardItemModel(this);
    active_list_model = new QStandardItemModel(this);

    load_settings();

    ui->tabbar->setShape(QTabBar::RoundedWest);

    // Load available ponies into the list
    reload_available_ponies();

    connect(ui->tabbar, SIGNAL(currentChanged(int)), this, SLOT(lettertab_changed(int)));

    ui->available_list->setIconSize(QSize(100,100));
    ui->available_list->setModel(list_model);
    ui->available_list->setAlternatingRowColors(true);

    ui->active_list->setIconSize(QSize(100,100));
    ui->active_list->setModel(active_list_model);
    ui->active_list->setAlternatingRowColors(true);

    connect(ui->available_list->selectionModel(), SIGNAL(currentChanged(QModelIndex,QModelIndex)), this, SLOT(newpony_list_changed(QModelIndex)));

    // Start update timer
    update_timer.setInterval(30);
    update_timer.start();

    interaction_timer.setInterval(500);
    interaction_timer.start();

    QObject::connect(&interaction_timer, SIGNAL(timeout()), this, SLOT(update_interactions()));

    // Load every pony specified in configuration
    QSettings settings;
    int size = settings.beginReadArray("loaded-ponies");
    for(int i=0; i< size; i++) {
        settings.setArrayIndex(i);
        try {
            ponies.emplace_back(std::make_shared<Pony>(settings.value("name").toString(), this));
            QObject::connect(&update_timer, SIGNAL(timeout()), ponies.back().get(), SLOT(update()));
        }catch (std::exception e) {
            std::cerr << "ERROR: Could not load pony '" << settings.value("name").toString() << "'." << std::endl;
        }
    }
    settings.endArray();
    list_model->sort(1);

    update_active_list();

    // Load interactions
    QFile ifile(getSetting<QString>("general/pony-directory") + "/interactions.ini");
    if(!ifile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::cerr << "ERROR: Cannot open interactions.ini" << std::endl;
        std::cerr << ifile.errorString() << std::endl;
    }

    if( ifile.isOpen() ) {
        QString line;
        QTextStream istr(&ifile);

        while (!istr.atEnd() ) {
            line = istr.readLine();

            if(line[0] != '\'' && !line.isEmpty()) {
                std::vector<QVariant> csv_data;
                CSVParser::ParseLine(csv_data, line, ',', Interaction::OptionTypes);
                try {
                    interactions.emplace_back(csv_data);
                }catch (std::exception e) {
                    std::cerr << "ERROR: Could not load interaction." << std::endl;
                }

            }
        }

        ifile.close();
    }else{
        std::cerr << "ERROR: Cannot read interactions.ini" << std::endl;
    }

}

ConfigWindow::~ConfigWindow()
{
    delete ui;
    delete signal_mapper;
    delete list_model;
    delete action_group;
}

void ConfigWindow::remove_pony()
{
    // Get a pointer to Pony from sender()
    QAction *q = qobject_cast<QAction*>(QObject::sender());
    Pony* p = static_cast<Pony*>(q->parent()->parent()); // QAction->QMenu->QMainWindow(Pony)
    ponies.remove(p->get_shared_ptr());

    save_settings();
    update_active_list();
}

void ConfigWindow::remove_pony_all()
{
    // Get a pointer to Pony from sender()
    QAction *q = qobject_cast<QAction*>(QObject::sender());
    Pony* p = static_cast<Pony*>(q->parent()->parent()); // QAction->QMenu->QMainWindow(Pony)
    QString pony_name(p->name); // We must copy the name, because it will be deleted
    ponies.remove_if([&pony_name](const std::shared_ptr<Pony> &pony){
        return pony->name == pony_name;
    });

    save_settings();
    update_active_list();
}

void ConfigWindow::remove_pony_activelist()
{

    // For each of the selected items
    for(auto &i: ui->active_list->selectionModel()->selectedIndexes() ) {
        if(i.column() == 0) {
            // Ignore the first column(with the icon), we are only interested in the second column (with the name of the pony)
            continue;
        }

        // Get the name from active list
        QString name = i.data().toString();

        // Find first occurance of pony name
        auto occurance = std::find_if(ponies.begin(), ponies.end(),
                                         [&name](const std::shared_ptr<Pony> &p)
                                         {
                                             return p->directory == name;
                                         });
        // If found, remove
        if(occurance != ponies.end()) {
            ponies.erase(occurance);
        }

    }

    save_settings();
    update_active_list();
}

void ConfigWindow::reload_available_ponies()
{
    QSettings settings;

    list_model->clear();
    int count = ui->tabbar->count();
    for(int i = 0; i < count; i++) {
        ui->tabbar->removeTab(0);
    }

    QDir dir(getSetting<QString>("general/pony-directory", settings) );
    dir.setFilter(QDir::Dirs | QDir::NoDotAndDotDot);

    // Get names of all the pony directories
    QList<QChar> letters;
    for(auto &i: dir.entryList()) {
        QDir pony_dir(dir);
        pony_dir.cd(i);
        if(pony_dir.exists("pony.ini")) {
            // Get the letters for TabBar for quick navigation of the available pony list
            if(!letters.contains(i[0])) {
                // Add the first letter of the name if we do not have it already
                letters.push_back(i[0]);
            }

            QStandardItem *item_icon = new QStandardItem(QIcon(pony_dir.absoluteFilePath("icon.png")),"");
            QStandardItem *item_text = new QStandardItem(i);

            QList<QStandardItem*> row;
            row << item_icon << item_text;
            list_model->appendRow(row);
        }
    }
    for(QChar &i: letters) ui->tabbar->addTab(i);


}

void ConfigWindow::newpony_list_changed(QModelIndex item)
{
    // Update the UI with information about selected pony
    ui->image_label->setPixmap(item.sibling(item.row(),0).data(Qt::DecorationRole).value<QIcon>().pixmap(100,100));
    ui->label_ponyname->setText(item.sibling(item.row(),1).data().toString());
}

void ConfigWindow::add_pony()
{
    // For each of the selected items
    for(auto &i: ui->available_list->selectionModel()->selectedIndexes() ) {
        if(i.column() == 0) {
            // Ignore the first column(with the icon), we are only interested in the second column (with the name of the pony)
            continue;
        }

        // Get the name from active list
        QString name = i.data().toString();

        try {
            // Try to initialize the new pony at the end of the active pony list and connect it to the update timer
            ponies.emplace_back(std::make_shared<Pony>(i.data().toString(), this));
            QObject::connect(&update_timer, SIGNAL(timeout()), ponies.back().get(), SLOT(update()));

        }catch (std::exception e) {
            std::cerr << "ERROR: Could not load pony '" << name << "'." << std::endl;
        }

    }

    save_settings();
    update_active_list();
}

void ConfigWindow::update_active_list()
{
    active_list_model->clear();
    for(auto &i: ponies) {
        QStandardItem *item_icon = new QStandardItem(QIcon(ConfigWindow::getSetting<QString>("general/pony-directory") + "/" + i->directory +  "/icon.png"),"");
        QStandardItem *item_text = new QStandardItem(i->directory);

        QList<QStandardItem*> row;
        row << item_icon << item_text;
        active_list_model->appendRow(row);
    }
    active_list_model->sort(1);
}

void ConfigWindow::lettertab_changed(int index)
{
    // Find items starting with the letter which is currently selected in the tab bar
    QList<QStandardItem *> found = list_model->findItems(ui->tabbar->tabText(index),Qt::MatchStartsWith, 1);
    if(!found.isEmpty()) { // It should always find something
        // Scroll active list to the first found item
        ui->available_list->scrollTo(found[0]->index(), QAbstractItemView::PositionAtTop);
    }
}

void ConfigWindow::toggle_window(QSystemTrayIcon::ActivationReason reason)
{
    // Toogle the configuration window's visibility
    if(reason == QSystemTrayIcon::DoubleClick) {
        if(this->isVisible() == true)
        {
            hide();
        }else{
            show();
        }
    }
}

void ConfigWindow::change_ponydata_directory()
{
    QString new_dir = QFileDialog::getExistingDirectory(this, tr("Select pony data directory"), getSetting<QString>("general/pony-directory"),
                                                        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks );

    if(new_dir != "") {
        ui->ponydata_directory->setText(new_dir);
    }
}

void ConfigWindow::load_settings()
{
    QSettings settings;

    // General settings
    ui->alwaysontop->setChecked(    getSetting<bool>    ("general/always-on-top",settings));
    ui->x11_bypass_wm->setChecked(  getSetting<bool>    ("general/bypass-wm",settings));
    ui->ponydata_directory->setText(getSetting<QString> ("general/pony-directory",settings));

    // Speech settings
    ui->speechenabled->setChecked(  getSetting<bool>    ("speech/enabled",settings));
    ui->textdelay->setValue(        getSetting<int>     ("speech/duration",settings));

    // Sound settings
    ui->playsounds->setChecked(     getSetting<bool>    ("sound/enabled",settings));

    // We do not load ponies here because we might use this function
    // to discard user made changes if user did not apply them
}

void ConfigWindow::save_settings()
{
    QSettings settings;

    // Check if we have to update the pony windows with new always-on-top/bypass-wm value
    bool change_ontop = (getSetting<bool>("general/always-on-top", settings) != ui->alwaysontop->isChecked());
    bool change_bypass_wm = (getSetting<bool>("general/bypass-wm", settings) != ui->x11_bypass_wm->isChecked());
    bool reload_ponies = (getSetting<QString>("general/pony-directory", settings) != ui->ponydata_directory->text());

    // Write the program settings
    settings.clear();

    // General settings
    settings.beginGroup("general");

    settings.setValue("always-on-top", ui->alwaysontop->isChecked());
    settings.setValue("bypass-wm", ui->x11_bypass_wm->isChecked());
    settings.setValue("pony-directory", ui->ponydata_directory->text());

    settings.endGroup();

    // Speech settings
    settings.beginGroup("speech");

    settings.setValue("enabled", ui->speechenabled->isChecked());
    settings.setValue("duration", ui->textdelay->value());

    settings.endGroup();

    // Sound settings
    settings.beginGroup("sound");

    settings.setValue("enabled", ui->playsounds->isChecked());

    settings.endGroup();

    // Write the active ponies list
    settings.beginWriteArray("loaded-ponies");
    int i=0;
    for(const auto &pony : ponies) {
        if(change_ontop) {
            pony->set_on_top(ui->alwaysontop->isChecked());
        }
        if(change_bypass_wm) {
            pony->set_bypass_wm(ui->x11_bypass_wm->isChecked());
        }
        settings.setArrayIndex(i);
        settings.setValue("name", pony->directory);
        i++;
    }
    settings.endArray();

    if(reload_ponies) {
        reload_available_ponies();
    }

    // Make sure we write our changes to disk
    settings.sync();
}

void ConfigWindow::update_distances()
{
    distances.clear();
    for(const std::shared_ptr<Pony> &p1: ponies) {
        QPoint c1(p1->x_pos + p1->current_behavior->x_center, p1->y_pos + p1->current_behavior->y_center);
        for(const std::shared_ptr<Pony> &p2: ponies) {
            if(p1 == p2) continue;

            QPoint c2(p2->x_pos + p2->current_behavior->x_center, p2->y_pos + p2->current_behavior->y_center);

            float dist = std::sqrt(std::pow(c2.x() - c1.x(), 2) + std::pow(c2.y() - c1.y(), 2));

            distances.insert({{p1->name, p2->name}, dist});
        }
    }
}

void ConfigWindow::update_interactions()
{
    if(!getSetting<bool>("general/interactions-enabled")) return;

    update_distances();

    // For each interaction
    for(auto &i: interactions){
        // check probability

        // For each pony that starts this interaction
        std::list<std::shared_ptr<Pony>>::iterator p;
        for(auto &p: ponies) {
            if(p->name != i.pony) continue;
            if(p->in_interaction) continue;
            if(p->next_interaction_time > QDateTime::currentMSecsSinceEpoch()) continue;

            // Here be dragons
            // A std::set of std::shared_ptr<Pony> with custom comparision function (p1->name < p2->name)
            // We use a set because its elements are unique, so when we add many ponies with the same name,
            //  only one will be stored
            // It is equivalent to:
            //  typedef std::function<bool(const std::shared_ptr<Pony> &p1, const std::shared_ptr<Pony> &p2) Comp;
            //  std::set<std::shared_ptr<Pony>, Comp>> interaction_targets
            // where interaction_targets are initialised with th comparision lambda function
            std::set<std::shared_ptr<Pony>, std::function<bool(const std::shared_ptr<Pony> &p1, const std::shared_ptr<Pony> &p2)>> interaction_targets
                                                   ( [](const std::shared_ptr<Pony> &p1, const std::shared_ptr<Pony> &p2) -> bool {return p1->name < p2->name;});

            bool break_noponies = true;

            // For each target of that interaction
            for(const QVariant &p_target: i.targets) {

                // For each found target
                for(auto &pp: ponies) {
                    if(pp == p) continue; // Do not interact with self
                    if(pp->name != p_target.toString()) continue;

                    if(distances.at(std::make_pair(p->name, p_target.toString())) > i.distance){
                        continue; // The pony is too far, check the rest
                    }else if(pp->in_interaction){
                        continue; // The pony is already in an interaction
                    }else{
                        break_noponies = false; // We found one/all suitable ponies, we can do the interaction
                        interaction_targets.insert(pp);
                    }
                }

                if(interaction_targets.empty()) {
                    // We didn't find anypony to interact with
                    break;
                }else if((i.random == false) && (interaction_targets.size() != static_cast<uint32_t>(i.targets.size()))){ // We cast to uint32_t because std::set is uint but QList is int
                    // We didn't find every required pony, so we abort
                    break_noponies = true;
                    break;
                    }
                }

            if(break_noponies) {
                continue; // We found no suitable ponies to do the interaction with, check the next initiating pony
            }

            // DO THE INTERACTION
            std::cout << p->name << " with:" << std::endl;
            for(auto &a: interaction_targets) {
                std::cout << " " << a->name << std::endl;
            }

        }
    }
}
