#include "uniforms.h"

#include <regex>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "phonedepth/extract_depthmap.h"

#include "tools/text.h"
#include "types/files.h"

#include "ada/pixel.h"
#include "ada/string.h"
#include "ada/gl/textureBump.h"
#include "ada/gl/textureStreamSequence.h"

#if defined(SUPPORT_MMAL)
#include "ada/gl/textureStreamMMAL.h"
#endif

#if defined(SUPPORT_LIBAV) 
#include "ada/gl/textureStreamAV.h"
#endif

#if defined(DRIVER_BROADCOM) && defined(SUPPORT_OMAX)
#include "ada/gl/textureStreamOMX.h"
#endif

std::string UniformData::getType() {
    if (size == 1) return (bInt ? "int" : "float");
    else return (bInt ? "ivec" : "vec") + ada::toString(size); 
}

void UniformData::set(const UniformValue &_value, size_t _size, bool _int ) {
    bInt = _int;
    size = _size;

    if (!change)
        value = _value;
    else
        queue.push( _value );
    change = true;
}

void UniformData::parse(const std::vector<std::string>& _command, size_t _start) {
    // bool isFloat = false;
    UniformValue candidate;
    for (size_t i = _start; i < _command.size() && i < 5; i++) {
        // isFloat += ada::isFloat(_command[i]);
        candidate[i-_start] = ada::toFloat(_command[i]);
    }
    // set(candidate, _command.size()-_start, !isFloat);
    set(candidate, _command.size()-_start, true);
}

bool UniformData::check() {
    if (queue.empty())
        change = false;
    else {
        value = queue.front();
        queue.pop();
        change = true;
    }
    return change;
}

UniformFunction::UniformFunction() {
    type = "-undefined-";
}

UniformFunction::UniformFunction(const std::string &_type) {
    type = _type;
}

UniformFunction::UniformFunction(const std::string &_type, std::function<void(ada::Shader&)> _assign) {
    type = _type;
    assign = _assign;
}

UniformFunction::UniformFunction(const std::string &_type, std::function<void(ada::Shader&)> _assign, std::function<std::string()> _print) {
    type = _type;
    assign = _assign;
    print = _print;
}

// UNIFORMS

Uniforms::Uniforms(): cubemap(nullptr), m_streamsPrevs(0), m_streamsPrevsChange(false), m_change(false), m_is_audio_init(false) {

    // set the right distance to the camera
    // Set up camera

    cameras.push_back( ada::Camera() );

    functions["u_iblLuminance"] = UniformFunction("float", [this](ada::Shader& _shader) {
        _shader.setUniform("u_iblLuminance", 30000.0f * getCamera().getExposure());
    },
    [this]() { return ada::toString(30000.0f * getCamera().getExposure()); });
    
    // CAMERA UNIFORMS
    //
    functions["u_camera"] = UniformFunction("vec3", [this](ada::Shader& _shader) {
        _shader.setUniform("u_camera", -getCamera().getPosition() );
    },
    [this]() { return ada::toString(-getCamera().getPosition(), ','); });

    functions["u_cameraDistance"] = UniformFunction("float", [this](ada::Shader& _shader) {
        _shader.setUniform("u_cameraDistance", getCamera().getDistance());
    },
    [this]() { return ada::toString(getCamera().getDistance()); });

    functions["u_cameraNearClip"] = UniformFunction("float", [this](ada::Shader& _shader) {
        _shader.setUniform("u_cameraNearClip", getCamera().getNearClip());
    },
    [this]() { return ada::toString(getCamera().getNearClip()); });

    functions["u_cameraFarClip"] = UniformFunction("float", [this](ada::Shader& _shader) {
        _shader.setUniform("u_cameraFarClip", getCamera().getFarClip());
    },
    [this]() { return ada::toString(getCamera().getFarClip()); });

    functions["u_cameraEv100"] = UniformFunction("float", [this](ada::Shader& _shader) {
        _shader.setUniform("u_cameraEv100", getCamera().getEv100());
    },
    [this]() { return ada::toString(getCamera().getEv100()); });

    functions["u_cameraExposure"] = UniformFunction("float", [this](ada::Shader& _shader) {
        _shader.setUniform("u_cameraExposure", getCamera().getExposure());
    },
    [this]() { return ada::toString(getCamera().getExposure()); });

    functions["u_cameraAperture"] = UniformFunction("float", [this](ada::Shader& _shader) {
        _shader.setUniform("u_cameraAperture", getCamera().getAperture());
    },
    [this]() { return ada::toString(getCamera().getAperture()); });

    functions["u_cameraShutterSpeed"] = UniformFunction("float", [this](ada::Shader& _shader) {
        _shader.setUniform("u_cameraShutterSpeed", getCamera().getShutterSpeed());
    },
    [this]() { return ada::toString(getCamera().getShutterSpeed()); });

    functions["u_cameraSensitivity"] = UniformFunction("float", [this](ada::Shader& _shader) {
        _shader.setUniform("u_cameraSensitivity", getCamera().getSensitivity());
    },
    [this]() { return ada::toString(getCamera().getSensitivity()); });

    functions["u_cameraChange"] = UniformFunction("bool", [this](ada::Shader& _shader) {
        _shader.setUniform("u_cameraChange", getCamera().bChange);
    },
    [this]() { return ada::toString(getCamera().getSensitivity()); });
    
    functions["u_normalMatrix"] = UniformFunction("mat3", [this](ada::Shader& _shader) {
        _shader.setUniform("u_normalMatrix", getCamera().getNormalMatrix());
    });

    // CAMERA MATRIX UNIFORMS
    functions["u_viewMatrix"] = UniformFunction("mat4", [this](ada::Shader& _shader) {
        _shader.setUniform("u_viewMatrix", getCamera().getViewMatrix());
    });

    functions["u_projectionMatrix"] = UniformFunction("mat4", [this](ada::Shader& _shader) {
        _shader.setUniform("u_projectionMatrix", getCamera().getProjectionMatrix());
    });
}

Uniforms::~Uniforms(){
}

bool Uniforms::parseLine( const std::string &_line ) {
    std::vector<std::string> values = ada::split(_line,',');
    if (values.size() > 1) {
        data[ values[0] ].parse(values, 1);
        m_change = true;
        return true;
    }
    return false;
}

bool Uniforms::addTexture( const std::string& _name, ada::Texture* _texture) {
    if (textures.find(_name) == textures.end()) {
        textures[ _name ] = _texture;
        return true;
    }
    else {
        if (textures[ _name ])
            delete textures[ _name ];
        textures[ _name ] = _texture;
    }
    return false;
}

bool Uniforms::addTexture(const std::string& _name, const std::string& _path, WatchFileList& _files, bool _flip, bool _verbose) {
    if (textures.find(_name) == textures.end()) {
        struct stat st;

        // If we can not get file stamp proably is not a file
        if (stat(_path.c_str(), &st) != 0 )
            std::cerr << "Error watching for file " << _path << std::endl;

        // If we can lets proceed creating a texgure
        else {

            ada::Texture* tex = new ada::Texture();
            // load an image into the texture
            if (tex->load(_path, _flip)) {
                
                // the image is loaded finish add the texture to the uniform list
                textures[ _name ] = tex;

                // and the file to the watch list
                WatchFile file;
                file.type = IMAGE;
                file.path = _path;
                file.lastChange = st.st_mtime;
                file.vFlip = _flip;
                _files.push_back(file);

                if (_verbose) {
                    std::cout << "// " << _path << " loaded as: " << std::endl;
                    std::cout << "uniform sampler2D   " << _name  << ";"<< std::endl;
                    std::cout << "uniform vec2        " << _name  << "Resolution;"<< std::endl;
                }

                if (ada::haveExt(_path,"jpeg")) {
                    const unsigned char *cv = NULL, *dm = NULL;
                    size_t cv_size = 0, dm_size = 0;
                    image_type_t dm_type = TYPE_NONE;

                    //  proceed to check if it have depth data
                    if (extract_depth(  _path.c_str(), 
                                        &cv, &cv_size,
                                        &dm, &dm_size, &dm_type) == 1) {

                        if (dm_type == TYPE_JPEG) {
                            int width, height;
                            unsigned char* pixels = ada::loadPixels(dm, dm_size, &width, &height, ada::RGB, _flip);

                            ada::Texture* tex_dm = new ada::Texture();
                            if (tex_dm->load(width, height, 3, 8, pixels)) {
                                textures[ _name + "Depth"] = tex_dm;

                                if (_verbose) {
                                    std::cout << "uniform sampler2D   " << _name  << "Depth;"<< std::endl;
                                    std::cout << "uniform vec2        " << _name  << "DepthResolution;"<< std::endl;
                                }
                            }   
                            ada::freePixels(pixels);
                        }
                    }
                }

                return true;
            }
            else
                delete tex;
        }
    }
    return false;
}

bool Uniforms::addBumpTexture(const std::string& _name, const std::string& _path, WatchFileList& _files, bool _flip, bool _verbose) {
    if (textures.find(_name) == textures.end()) {
        struct stat st;

        // If we can not get file stamp proably is not a file
        if (stat(_path.c_str(), &st) != 0 )
            std::cerr << "Error watching for file " << _path << std::endl;
        
        // If we can lets proceed creating a texgure
        else {
            ada::TextureBump* tex = new ada::TextureBump();

            // load an image into the texture
            if (tex->load(_path, _flip)) {

                // the image is loaded finish add the texture to the uniform list
                textures[ _name ] = (ada::Texture*)tex;

                // and the file to the watch list
                WatchFile file;
                file.type = IMAGE_BUMPMAP;
                file.path = _path;
                file.lastChange = st.st_mtime;
                file.vFlip = _flip;
                _files.push_back(file);

                if (_verbose) {
                    std::cout << "// " << _path << " loaded and transform to normalmap as: " << std::endl;
                    std::cout << "uniform sampler2D   " << _name  << ";"<< std::endl;
                    std::cout << "uniform vec2        " << _name  << "Resolution;"<< std::endl;
                }

                return true;
            }
            else
                delete tex;
        }
    }
    return false;
}

bool Uniforms::addStreamingTexture( const std::string& _name, const std::string& _url, bool _vflip, bool _device, bool _verbose) {
    if (textures.find(_name) == textures.end()) {

        // Check if it's a PNG Sequence
        if ( checkPattern(_url) ) {
            ada::TextureStreamSequence *tex = new ada::TextureStreamSequence();

            if (tex->load(_url, _vflip)) {
                // the image is loaded finish add the texture to the uniform list
                textures[ _name ] = (ada::Texture*)tex;
                streams[ _name ] = (ada::TextureStream*)tex;

                if (_verbose) {
                    std::cout << "// " << _url << " sequence loaded as streaming texture: " << std::endl;
                    std::cout << "uniform sampler2D   " << _name  << ";"<< std::endl;
                    std::cout << "uniform sampler2D   " << _name  << "Prev[STREAMS_PREVS];"<< std::endl;
                    std::cout << "uniform vec2        " << _name  << "Resolution;"<< std::endl;
                    std::cout << "uniform float       " << _name  << "Time;" << std::endl;
                    std::cout << "uniform float       " << _name  << "Duration;" << std::endl;
                    std::cout << "uniform float       " << _name  << "CurrentFrame;"<< std::endl;
                    std::cout << "uniform float       " << _name  << "TotalFrames;"<< std::endl;
                    std::cout << "uniform float       " << _name  << "Fps;"<< std::endl;
                }

                return true;
            }
            else
                delete tex;
            
        }
#if defined(SUPPORT_MMAL)
        // if the user is asking for a device on a RaspberryPI hardware
        else if (_device) {
            ada::TextureStreamMMAL* tex = new ada::TextureStreamMMAL();

            // load an image into the texture
            if (tex->load(_url, _vflip)) {
                // the image is loaded finish add the texture to the uniform list
                textures[ _name ] = (ada::Texture*)tex;
                streams[ _name ] = (ada::TextureStream*)tex;

                if (_verbose) {
                    std::cout << "// " << _url << " loaded as streaming texture: " << std::endl;
                    std::cout << "uniform sampler2D   " << _name  << ";"<< std::endl;
                    std::cout << "uniform vec2        " << _name  << "Resolution;"<< std::endl;
                }

                return true;
            }
            else
                delete tex;
        }
#endif
#if defined(DRIVER_BROADCOM) && defined(SUPPORT_OMAX)
        else if ( ada::haveExt(_url,"h264") || ada::haveExt(_url,"H264") ) {
            ada::TextureStreamOMX* tex = new ada::TextureStreamOMX();

            // load an image into the texture
            if (tex->load(_url, _vflip)) {
                // the image is loaded finish add the texture to the uniform list
                textures[ _name ] = (ada::Texture*)tex;
                streams[ _name ] = (ada::TextureStream*)tex;

                if (_verbose) {
                    std::cout << "// " << _url << " loaded as streaming texture: " << std::endl;
                    std::cout << "uniform sampler2D   " << _name  << ";"<< std::endl;
                    std::cout << "uniform vec2        " << _name  << "Resolution;"<< std::endl;
                }

                return true;
            }
            else
                delete tex;
        }
#endif
        else {
#if defined(SUPPORT_LIBAV)
        ada::TextureStreamAV* tex = new ada::TextureStreamAV(_device);

        // load an image into the texture
        if (tex->load(_url, _vflip)) {
            // the image is loaded finish add the texture to the uniform list
            textures[ _name ] = (ada::Texture*)tex;
            streams[ _name ] = (ada::TextureStream*)tex;

            if (_verbose) {
                std::cout << "// " << _url << " loaded as streaming texture: " << std::endl;
                std::cout << "uniform sampler2D   " << _name  << ";"<< std::endl;
                std::cout << "uniform sampler2D   " << _name  << "Prev[STREAMS_PREVS];"<< std::endl;
                std::cout << "uniform vec2        " << _name  << "Resolution;"<< std::endl;

                if (!_device) {
                    std::cout << "uniform float       " << _name  << "Time;" << std::endl;
                    std::cout << "uniform float       " << _name  << "Duration;" << std::endl;
                    std::cout << "uniform float       " << _name  << "CurrentFrame;" << std::endl;
                    std::cout << "uniform float       " << _name  << "TotalFrames;" << std::endl;
                    std::cout << "uniform float       " << _name  << "Fps;" << std::endl;
                }
            }

            return true;
        }
        else
            delete tex;
#endif
        }


    }
    return false;
}

bool Uniforms::addAudioTexture(const std::string& _name, const std::string& device_id, bool _flip, bool _verbose) {

#if defined(SUPPORT_LIBAV)
    // already init
    if (m_is_audio_init) return false;

    TextureStreamAudio *tex = new TextureStreamAudio();

    // TODO: add flipping mode for audio texture
    if (tex->load(device_id, _flip)) {

        if (_verbose) {
            std::cout << "loaded audio texture: " << std::endl;
            std::cout << "uniform sampler2D   " << _name  << ";"<< std::endl;
            std::cout << "uniform vec2        " << _name  << "Resolution;"<< std::endl;
        }
            textures[ _name ] = (ada::Texture*)tex;
            streams[ _name ] = (ada::TextureStream*)tex;
            m_is_audio_init = true;
            return true;
    }
    else
#endif
        return false;
}

bool Uniforms::addCameraTrack( const std::string& _name ) {
    std::fstream is( _name.c_str(), std::ios::in);
    if (is.is_open()) {

        glm::vec3 position;
        cameraTrack.clear();

        std::string line;
        // glm::mat4 rot = glm::mat4(
        //         1.0f,   0.0f,   0.0f,   0.0f,
        //         0.0f,  1.0f,    0.0f,   0.0f,
        //         0.0f,   0.0f,   -1.0f,   0.0f,
        //         0.0f,   0.0f,   0.0f,   1.0f
        //     );


        while (std::getline(is, line)) {
            // If line not commented 
            if (line[0] == '#')
                continue;

            // parse through row spliting into commas
            std::vector<std::string> params = ada::split(line, ',', true);

            CameraData frame;
            float fL = ada::toFloat(params[0]);
            float cx = ada::toFloat(params[1]);
            float cy = ada::toFloat(params[2]);

            float near = cameras[0].getNearClip();
            float far = cameras[0].getFarClip();
            float delta = far-near;

            float w = cx*2.0;
            float h = cy*2.0;

            // glm::mat4 projection = glm::ortho(0.0f, w, h, 0.0f, near, far);
            // glm::mat4 ndc = glm::mat4(
            //     fL,     0.0f,   0.0f,   0.0f,
            //     0.0f,   fL,     0.0f,   0.0f, 
            //     cx,     cy,     1.0f,   0.0f,
            //     0.0f,   0.0f,   0.0f,   1.0f  
            // );
            // frame.projection = projection * ndc;

            frame.projection = glm::mat4(
                2.0f*fL/w,          0.0f,               0.0f,                       0.0f,
                0.0f,               -2.0f*fL/h,         0.0f,                       0.0f,
                (w - 2.0f*cx)/w,    (h-2.0f*cy)/h,      (-far-near)/delta,         -1.0f,
                0.0f,               0.0f,               -2.0f*far*near/delta,       0.0f
            );
            

            // frame.projection = glm::mat4(
            //     fL/cx,      0.0f,   0.0f,                   0.0f,
            //     0.0f,       fL/cy,  0.0f,                   0.0f,
            //     0.0f,       0.0f,   -(far+near)/delta,      -1.0,
            //     0.0f,       0.0f,   -2.0*far*near/delta,    0.0f
            // );

            frame.transform = glm::mat4(
                glm::vec4( ada::toFloat(params[3]), ada::toFloat(params[ 4]), ada::toFloat(params[ 5]), 0.0),
                glm::vec4( ada::toFloat(params[6]), -ada::toFloat(params[ 7]), ada::toFloat(params[ 8]), 0.0),
                glm::vec4( ada::toFloat(params[9]), ada::toFloat(params[10]), ada::toFloat(params[11]), 0.0),
                glm::vec4( ada::toFloat(params[12]), ada::toFloat(params[13]), -ada::toFloat(params[14]), 1.0)
            );

            // position += frame.translation;
            cameraTrack.push_back(frame);
        }

        std::cout << "// Added " << cameraTrack.size() << " camera frames" << std::endl;

        position = position / float(cameraTrack.size());
        return true;
    }

    return false;
}

void Uniforms::setStreamPlay( const std::string& _name) {
    if (streams.find(_name) != streams.end())
        streams[_name]->play();
}

void Uniforms::setStreamStop( const std::string& _name) {
    if (streams.find(_name) != streams.end())
        streams[_name]->stop();
}

void Uniforms::setStreamRestart( const std::string& _name ) {
    if (streams.find(_name) != streams.end())
        streams[_name]->restart();
}

float Uniforms::getStreamTime( const std::string& _name) {
    if (streams.find(_name) != streams.end())
        return streams[_name]->getTime();
    return 0.0f;
}

float Uniforms::getStreamSpeed( const std::string& _name) {
    if (streams.find(_name) != streams.end())
        return streams[_name]->getSpeed();
    return 0.0f;
}

void Uniforms::setStreamSpeed( const std::string& _name, float _speed ) {
    if (streams.find(_name) != streams.end())
        streams[_name]->setSpeed(_speed);
}

float Uniforms::getStreamPct( const std::string& _name) {
    if (streams.find(_name) != streams.end())
        return streams[_name]->getPct();
    return 0.0f;
}

void Uniforms::setStreamPct( const std::string& _name, float _pct ) {
    if (streams.find(_name) != streams.end())
        streams[_name]->setPct(_pct);
}

void Uniforms::setStreamTime( const std::string& _name, float _time ) {
    if (streams.find(_name) != streams.end())
        streams[_name]->setTime(_time);
}

void Uniforms::setStreamsPlay() {
    for (StreamsList::iterator i = streams.begin(); i != streams.end(); ++i)
        i->second->play();
}

void Uniforms::setStreamsStop() {
    for (StreamsList::iterator i = streams.begin(); i != streams.end(); ++i)
        i->second->stop();
}

void Uniforms::setStreamsRestart() {
    for (StreamsList::iterator i = streams.begin(); i != streams.end(); ++i)
        i->second->restart();
}

void Uniforms::setStreamsSpeed( float _speed ) {
    for (StreamsList::iterator i = streams.begin(); i != streams.end(); ++i)
        i->second->setSpeed(_speed);
}

void Uniforms::setStreamsTime( float _time ) {
    for (StreamsList::iterator i = streams.begin(); i != streams.end(); ++i)
        i->second->setTime(_time);
}

void Uniforms::setStreamsPct( float _pct ) {
    for (StreamsList::iterator i = streams.begin(); i != streams.end(); ++i)
        i->second->setPct(_pct);
}

void Uniforms::setStreamsPrevs( size_t _total ) {
    m_streamsPrevs = _total;
    m_streamsPrevsChange = true;
}

void Uniforms::updateStreams(size_t _frame) {

    if (m_streamsPrevsChange) {
        m_streamsPrevsChange = false;
        for (StreamsList::iterator i = streams.begin(); i != streams.end(); i++)
            i->second->setPrevTextures(m_streamsPrevs);
    }

    for (StreamsList::iterator i = streams.begin(); i != streams.end(); ++i)
        if (i->second->update())
            m_change = true;

    if (cameraTrack.size() != 0) {
        size_t index = _frame % cameraTrack.size();
        cameras[0].setProjection( cameraTrack[index].projection ); 
        cameras[0].setTransformMatrix( cameraTrack[index].transform );
    } 
}

void Uniforms::set( const std::string& _name, float _value) {
    UniformValue value;
    value[0] = _value;
    data[_name].set(value, 1, false);
    m_change = true;
}

void Uniforms::set(const std::string& _name, float _x, float _y) {
    UniformValue value;
    value[0] = _x;
    value[1] = _y;
    data[_name].set(value, 2, false);
    m_change = true;
}

void Uniforms::set(const std::string& _name, float _x, float _y, float _z) {
    UniformValue value;
    value[0] = _x;
    value[1] = _y;
    value[2] = _z;
    data[_name].set(value, 3, false);
    m_change = true;
}

void Uniforms::set(const std::string& _name, float _x, float _y, float _z, float _w) {
    UniformValue value;
    value[0] = _x;
    value[1] = _y;
    value[2] = _z;
    value[3] = _w;
    data[_name].set(value, 4, false);
    m_change = true;
}

void Uniforms::setCubeMap( ada::TextureCube* _cubemap ) {
    if (cubemap)
        delete cubemap;
    cubemap = _cubemap;
}

void Uniforms::setCubeMap( const std::string& _filename, WatchFileList& _files, bool _verbose ) {
    struct stat st;
    if ( stat(_filename.c_str(), &st) != 0 ) {
        std::cerr << "Error watching for cubefile: " << _filename << std::endl;
    }
    else {
        ada::TextureCube* tex = new ada::TextureCube();
        if ( tex->load(_filename, true) ) {

            setCubeMap(tex);

            WatchFile file;
            file.type = CUBEMAP;
            file.path = _filename;
            file.lastChange = st.st_mtime;
            file.vFlip = true;
            _files.push_back(file);

            std::cout << "// " << _filename << " loaded as: " << std::endl;
            std::cout << "uniform samplerCube u_cubeMap;"<< std::endl;
            std::cout << "uniform vec3        u_SH[9];"<< std::endl;

        }
        else {
            delete tex;
        }
    }
}

void Uniforms::checkPresenceIn( const std::string &_vert_src, const std::string &_frag_src ) {
    // Check active native uniforms
    for (UniformFunctionsList::iterator it = functions.begin(); it != functions.end(); ++it) {
        std::string name = it->first + ";";
        bool present = ( findId(_vert_src, name.c_str()) || findId(_frag_src, name.c_str()) );
        if ( it->second.present != present) {
            it->second.present = present;
            m_change = true;
        } 
    }
}

bool Uniforms::feedTo(ada::Shader &_shader, bool _lights, bool _buffers ) {
    return feedTo(&_shader, _lights, _buffers);
}

bool Uniforms::feedTo(ada::Shader *_shader, bool _lights, bool _buffers ) {
    bool update = false;

    // Pass Native uniforms 
    for (UniformFunctionsList::iterator it=functions.begin(); it!=functions.end(); ++it) {
        if (!_lights && ( it->first == "u_scene" || "u_sceneDepth") )
            continue;

        if (it->second.present)
            if (it->second.assign)
                it->second.assign( *_shader );
    }

    // Pass User defined uniforms
    if (m_change) {
        for (UniformDataList::iterator it=data.begin(); it!=data.end(); ++it) {
            if (it->second.change) {
                // if (it->second.bInt) {
                //     if (it->second.size == 1)
                //         _shader->setUniform(it->first, int(it->second.value[0]));
                //     else if (it->second.size == 2)
                //         _shader->setUniform(it->first, int(it->second.value[0]), int(it->second.value[1]));
                //     else if (it->second.size == 3)
                //         _shader->setUniform(it->first, int(it->second.value[0]), int(it->second.value[1]), int(it->second.value[2]));
                //     else if (it->second.size == 4)
                //         _shader->setUniform(it->first, int(it->second.value[0]), int(it->second.value[1]), int(it->second.value[2]), int(it->second.value[3]));
                // }
                // else
                    _shader->setUniform(it->first, it->second.value.data(), it->second.size);
                update += true;
            }
        }
    }

    // Pass Textures Uniforms
    for (TextureList::iterator it = textures.begin(); it != textures.end(); ++it) {
        _shader->setUniformTexture(it->first, it->second, _shader->textureIndex++ );
        _shader->setUniform(it->first+"Resolution", float(it->second->getWidth()), float(it->second->getHeight()));
    }

    for (StreamsList::iterator it = streams.begin(); it != streams.end(); ++it) {
        for (size_t i = 0; i < it->second->getPrevTexturesTotal(); i++)
            _shader->setUniformTexture(it->first+"Prev["+ada::toString(i)+"]", it->second->getPrevTextureId(i), _shader->textureIndex++);

        _shader->setUniform(it->first+"Time", float(it->second->getTime()));
        _shader->setUniform(it->first+"Fps", float(it->second->getFps()));
        _shader->setUniform(it->first+"Duration", float(it->second->getDuration()));
        _shader->setUniform(it->first+"CurrentFrame", float(it->second->getCurrentFrame()));
        _shader->setUniform(it->first+"TotalFrames", float(it->second->getTotalFrames()));
    }

    // Pass Buffers Texture
    if (_buffers) {
        for (size_t i = 0; i < buffers.size(); i++)
            _shader->setUniformTexture("u_buffer" + ada::toString(i), &buffers[i], _shader->textureIndex++ );

        for (size_t i = 0; i < doubleBuffers.size(); i++)
            _shader->setUniformTexture("u_doubleBuffer" + ada::toString(i), doubleBuffers[i].src, _shader->textureIndex++ );
    }

    // Pass Convolution Piramids resultant Texture
    for (size_t i = 0; i < convolution_pyramids.size(); i++)
        _shader->setUniformTexture("u_convolutionPyramid" + ada::toString(i), convolution_pyramids[i].getResult(), _shader->textureIndex++ );
    
    if (_lights) {
        // Pass Light Uniforms
        if (lights.size() == 1) {
            if (lights[0].getType() != ada::LIGHT_DIRECTIONAL)
                _shader->setUniform("u_light", lights[0].getPosition());
            _shader->setUniform("u_lightColor", lights[0].color);
            if (lights[0].getType() == ada::LIGHT_DIRECTIONAL || lights[0].getType() == ada::LIGHT_SPOT)
                _shader->setUniform("u_lightDirection", lights[0].direction);
            _shader->setUniform("u_lightIntensity", lights[0].intensity);
            if (lights[0].falloff > 0)
                _shader->setUniform("u_lightFalloff", lights[0].falloff);
            _shader->setUniform("u_lightMatrix", lights[0].getBiasMVPMatrix() );
            _shader->setUniformDepthTexture("u_lightShadowMap", lights[0].getShadowMap(), _shader->textureIndex++ );
        }
        else {
            for (size_t i = 0; i < lights.size(); i++) {
                if (lights[i].getType() != ada::LIGHT_DIRECTIONAL)
                    _shader->setUniform("u_light", lights[i].getPosition());
                _shader->setUniform("u_lightColor", lights[i].color);
                if (lights[i].getType() == ada::LIGHT_DIRECTIONAL || lights[i].getType() == ada::LIGHT_SPOT)
                    _shader->setUniform("u_lightDirection", lights[i].direction);
                _shader->setUniform("u_lightIntensity", lights[i].intensity);
                if (lights[i].falloff > 0)
                    _shader->setUniform("u_lightFalloff", lights[i].falloff);
                _shader->setUniform("u_lightMatrix", lights[i].getBiasMVPMatrix() );
                _shader->setUniformDepthTexture("u_lightShadowMap", lights[i].getShadowMap(), _shader->textureIndex++ );
            }
        }
        
        if (cubemap) {
            _shader->setUniformTextureCube("u_cubeMap", (ada::TextureCube*)cubemap);
            _shader->setUniform("u_SH", cubemap->SH, 9);
        }
    }


    return update;
}

void Uniforms::flagChange() {
    // Flag all user uniforms as changed
    for (UniformDataList::iterator it = data.begin(); it != data.end(); ++it)
        it->second.change = true;
    
    m_change = true;
    getCamera().bChange = true;
}

void Uniforms::unflagChange() {
    if (m_change) {
        m_change = false;

        // Flag all user uniforms as NOT changed
        for (UniformDataList::iterator it = data.begin(); it != data.end(); ++it)
            m_change += it->second.check();
    }

    for (size_t i = 0; i < lights.size(); i++)
        lights[i].bChange = false;

    getCamera().bChange = false;
}

bool Uniforms::haveChange() { 
    bool lightChange = false;
    for (size_t i = 0; i < lights.size(); i++) {
        if (lights[i].bChange) {
            lightChange = true;
            break;
        }
    }

    return  m_change || 
            streams.size() > 0 ||
            functions["u_time"].present || 
            functions["u_delta"].present ||
            functions["u_mouse"].present ||
            functions["u_date"].present ||
            lightChange || getCamera().bChange;
}

void Uniforms::clear() {
    if (cubemap) {
        delete cubemap;
        cubemap = nullptr;
    }

    // Delete Textures
    for (TextureList::iterator i = textures.begin(); i != textures.end(); ++i) {
        if (i->second) {
            delete i->second;
            i->second = nullptr;
        }
    }
    textures.clear();

    // Streams are textures so it should be clear by now;
    // streams.clear();
}

void Uniforms::printAvailableUniforms(bool _non_active) {
    if (_non_active) {
        // Print all Native Uniforms (they carry functions)
        for (UniformFunctionsList::iterator it= functions.begin(); it != functions.end(); ++it) {                
            std::cout << "uniform " << it->second.type << ' ' << it->first << ";";
            if (it->second.print) 
                std::cout << " // " << it->second.print();
            std::cout << std::endl;
        }
    }
    else {
        // Print Native Uniforms (they carry functions) that are present on the shader
        for (UniformFunctionsList::iterator it= functions.begin(); it != functions.end(); ++it) {                
            if (it->second.present) {
                std::cout<< "uniform " << it->second.type << ' ' << it->first << ";";
                if (it->second.print)
                    std::cout << " // " << it->second.print();
                std::cout << std::endl;
            }
        }
    }
}

void Uniforms::printDefinedUniforms(bool _csv){
    // Print user defined uniform data
    if (_csv) {
        for (UniformDataList::iterator it= data.begin(); it != data.end(); ++it) {
            std::cout << it->first;
            for (int i = 0; i < it->second.size; i++) {
                std::cout << ',' << it->second.value[i];
            }
            std::cout << std::endl;
        }
    }
    else {
        for (UniformDataList::iterator it= data.begin(); it != data.end(); ++it) {
            std::cout << "uniform " << it->second.getType() << "  " << it->first << ";";
            for (int i = 0; i < it->second.size; i++)
                std::cout << ((i == 0)? " // " : "," ) << it->second.value[i];
            
            std::cout << std::endl;
        }
    }
    
}

void Uniforms::printBuffers() {
    for (size_t i = 0; i < buffers.size(); i++)
        std::cout << "uniform sampler2D u_buffer" << i << ";" << std::endl;

    for (size_t i = 0; i < doubleBuffers.size(); i++)
        std::cout << "uniform sampler2D u_doubleBuffer" << i << ";" << std::endl;

    for (size_t i = 0; i < convolution_pyramids.size(); i++)
        std::cout << "uniform sampler2D u_convolutionPyramid" << i << ";" << std::endl;
    
    if (functions["u_scene"].present)
        std::cout << "uniform sampler2D u_scene;" << std::endl;

    if (functions["u_sceneDepth"].present)
        std::cout << "uniform sampler2D u_sceneDepth;" << std::endl;
}

void Uniforms::printTextures() {
    for (TextureList::iterator it = textures.begin(); it != textures.end(); ++it) {
        std::cout << "uniform sampler2D " << it->first << "; // " << it->second->getFilePath() << std::endl;
        std::cout << "uniform vec2 " << it->first << "Resolution; // " << ada::toString(it->second->getWidth(), 1) << "," << ada::toString(it->second->getHeight(), 1) << std::endl;
    }
}

void Uniforms::printStreams() {
    for (StreamsList::iterator it = streams.begin(); it != streams.end(); ++it) {
        std::cout << "uniform sampler2D " << it->first << "; // " << it->second->getFilePath() << std::endl;

        if (m_streamsPrevs > 0)
            std::cout << "uniform sampler2D " << it->first << "Prev;" << std::endl;

        std::cout << "uniform float " << it->first+"CurrentFrame; //" << ada::toString(it->second->getCurrentFrame(), 1) << std::endl;
        std::cout << "uniform float " << it->first+"TotalFrames;  //" << ada::toString(it->second->getTotalFrames(), 1) << std::endl;
        std::cout << "uniform float " << it->first+"Time;         // " << ada::toString(it->second->getTime(), 1) << std::endl;
        std::cout << "uniform float " << it->first+"Duration;     // " << ada::toString(it->second->getDuration(), 1) << std::endl;
        std::cout << "uniform float " << it->first+"Fps;          // " << ada::toString(it->second->getFps(), 1) << std::endl;
    }
}

void Uniforms::printLights() {
    if (lights.size() == 1) {
        if (lights[0].getType() != ada::LIGHT_DIRECTIONAL)
            std::cout << "unifrom vect3 u_light; // " << ada::toString( lights[0].getPosition() ) << std::endl;
        std::cout << "unifrom vect3 u_lightColor; // " << ada::toString( lights[0].color )  << std::endl;
        if (lights[0].getType() == ada::LIGHT_DIRECTIONAL || lights[0].getType() == ada::LIGHT_SPOT)
            std::cout << "unifrom vect3 u_lightDirection; // " << ada::toString( lights[0].direction ) << std::endl;
        std::cout << "unifrom float u_lightIntensity; // " << ada::toString( lights[0].intensity, 3) << std::endl;
        if (lights[0].falloff > 0.0)
            std::cout << "unifrom float u_lightFalloff; // " << ada::toString( lights[0].falloff, 3) << std::endl;
    }
    else if (lights.size() > 1) {
        for (size_t i = 0; i < lights.size(); i++) {
            if (lights[i].getType() != ada::LIGHT_DIRECTIONAL)
                std::cout << "uniform vec3 u_light; // " << ada::toString( lights[i].getPosition() ) << std::endl;
            std::cout << "uniform vec3 u_lightColor; // " << ada::toString( lights[i].color )  << std::endl;
            if (lights[i].getType() == ada::LIGHT_DIRECTIONAL || lights[i].getType() == ada::LIGHT_SPOT)
                std::cout << "uniform vec3 u_lightDirection; // " << ada::toString( lights[i].direction ) << std::endl;
            std::cout << "uniform float u_lightIntensity; // " << ada::toString( lights[i].intensity, 3) << std::endl;
            if (lights[i].falloff > 0.0)
                std::cout << "uniform float u_lightFalloff; // " << ada::toString( lights[i].falloff, 3) << std::endl;
        }
    }
}
