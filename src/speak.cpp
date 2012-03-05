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

#include <sstream>
#include <iostream>

#ifdef USE_PHONON
 #include <Phonon/MediaObject>
 #include <Phonon/AudioOutput>
#endif

#include "speak.h"
#include "pony.h"

// These are the variable types for Behavior configuration
const CSVParser::ParseTypes Speak::OptionTypes = {
    {                     "type", QVariant::Type::String },
    {                     "name", QVariant::Type::String },
    {                     "text", QVariant::Type::String },
    {                    "files", QVariant::Type::String },
    {            "skip_normally", QVariant::Type::Bool }
};

Speak::Speak(Pony* parent, const QString filepath, const std::vector<QVariant> &options)
    :QObject(parent), parent(parent), path(filepath), audioOutput(nullptr), mediaObject(nullptr)
{

    if(options.size() == 2) { // Speak, "text"
        text = options[1].toString();
        skip_normally = false;
    }else{ // Speak, name, "text"
        name = options[1].toString().toLower();
        text = options[2].toString();

        if(options.size()>3){ // Speak, name, "text", {"file.mp3", "file.ogg"}, skip_normally
            if(options[3] != "") {
                soundfiles = qVariantValue<QList<QVariant>>(options[3]);
            }

//            skip_normally = (options[4].toString().compare("true", Qt::CaseInsensitive) == 0)?true:false;
            skip_normally = options[4].toBool();
        }
    }
}

Speak::~Speak()
{
#ifdef USE_PHONON
    if(mediaObject != nullptr) {
        mediaObject->stop();
        delete mediaObject;
    }

    if(audioOutput != nullptr) delete audioOutput;
#endif
}

void Speak::play()
{
#ifdef USE_PHONON
    if(soundfiles.size() == 0) return;

    if(audioOutput == nullptr) {
        audioOutput = new Phonon::AudioOutput(Phonon::MusicCategory, this);
    }
    if(mediaObject == nullptr) {
        mediaObject = new Phonon::MediaObject(this);
    }

    mediaObject->setCurrentSource("desktop-ponies/" + path + "/" + soundfiles[0].toString());
    connect(mediaObject, SIGNAL(finished()), this, SLOT(stop()));

    Phonon::createPath(mediaObject, audioOutput);

    mediaObject->play();
#endif
}

void Speak::stop()
{
#ifdef USE_PHONON
    if(mediaObject != nullptr) {
        mediaObject->stop();
        delete mediaObject;
        mediaObject = nullptr;
    }
    if(audioOutput != nullptr) {
        delete audioOutput;
        mediaObject = nullptr;
    }
#endif
}
