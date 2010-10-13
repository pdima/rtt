/***************************************************************************
  tag: The SourceWorks  Tue Sep 7 00:55:18 CEST 2010  PluginLoader.cpp

                        PluginLoader.cpp -  description
                           -------------------
    begin                : Tue September 07 2010
    copyright            : (C) 2010 The SourceWorks
    email                : peter@thesourceworks.com

 ***************************************************************************
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU General Public                   *
 *   License as published by the Free Software Foundation;                 *
 *   version 2 of the License.                                             *
 *                                                                         *
 *   As a special exception, you may use this file as part of a free       *
 *   software library without restriction.  Specifically, if other files   *
 *   instantiate templates or use macros or inline functions from this     *
 *   file, or you compile this file and link it with other files to        *
 *   produce an executable, this file does not by itself cause the         *
 *   resulting executable to be covered by the GNU General Public          *
 *   License.  This exception does not however invalidate any other        *
 *   reasons why the executable file might be covered by the GNU General   *
 *   Public License.                                                       *
 *                                                                         *
 *   This library is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU     *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU General Public             *
 *   License along with this library; if not, write to the Free Software   *
 *   Foundation, Inc., 59 Temple Place,                                    *
 *   Suite 330, Boston, MA  02111-1307  USA                                *
 *                                                                         *
 ***************************************************************************/


/**
 * PluginLoader.cpp
 *
 *  Created on: May 19, 2010
 *      Author: kaltan
 */

#include "PluginLoader.hpp"
#include "../TaskContext.hpp"
#include "../Logger.hpp"
#include <boost/filesystem.hpp>
#include "../../os/StartStopManager.hpp"

#include <cstdlib>
#include <dlfcn.h>


using namespace RTT;
using namespace plugin;
using namespace std;
using namespace boost::filesystem;

// chose the file extension applicable to the O/S
#ifdef  __APPLE__
static const std::string SO_EXT(".dylib");
#else
# ifdef _WIN32
static const std::string SO_EXT(".dll");
# else
static const std::string SO_EXT(".so");
# endif
#endif

// choose how the PATH looks like
# ifdef _WIN32
static const std::string delimiters(";");
static const std::string default_delimiter(";");
# else
static const std::string delimiters(":;");
static const std::string default_delimiter(":");
# endif


namespace {
    /**
     * Reads the RTT_COMPONENT_PATH and inits the PluginLoader.
     */
    int loadPlugins()
    {
        char* paths = getenv("RTT_COMPONENT_PATH");
        if (paths) {
            string plugin_paths = paths;
            log(Info) <<"RTT_COMPONENT_PATH was set to " << plugin_paths << endlog();
            PluginLoader::Instance()->setPluginPath(plugin_paths);
        } else {
            log(Info) <<"No RTT_COMPONENT_PATH set." <<endlog();
        }
        PluginLoader::Instance()->loadPlugins("");
        PluginLoader::Instance()->loadTypekits("");
        return 0;
    }

    os::InitFunction plugin_loader( &loadPlugins );

    void unloadPlugins()
    {
        PluginLoader::Release();
    }

    os::CleanupFunction plugin_unloader( &unloadPlugins );
}

boost::shared_ptr<PluginLoader> PluginLoader::minstance;

namespace {

vector<string> splitPaths(string const& str)
{
    vector<string> paths;

    // Skip delimiters at beginning.
    string::size_type lastPos = str.find_first_not_of(delimiters, 0);
    // Find first "non-delimiter".
    string::size_type pos = str.find_first_of(delimiters, lastPos);

    while (string::npos != pos || string::npos != lastPos)
    {
        // Found a token, add it to the vector.
        if ( !str.substr(lastPos, pos - lastPos).empty() )
            paths.push_back(str.substr(lastPos, pos - lastPos));
        // Skip delimiters.  Note the "not_of"
        lastPos = str.find_first_not_of(delimiters, pos);
        // Find next "non-delimiter"
        pos = str.find_first_of(delimiters, lastPos);
    }
    if ( paths.empty() )
        paths.push_back(".");
    return paths;
}

/**
 * Strips the 'lib' prefix and '.so'/'.dll'/... suffix (ie SO_EXT) from a filename.
 * Do not provide paths, only filenames, for example: "libplugin.so"
 * @param str filename.
 * @return stripped filename.
 */
string makeShortFilename(string const& str) {
    string ret = str;
    if (str.substr(0,3) == "lib")
        ret = str.substr(3);
    if (ret.rfind(SO_EXT) != string::npos)
        ret = ret.substr(0, ret.rfind(SO_EXT) );
    return ret;
}

}

PluginLoader::PluginLoader() { log(Debug) <<"PluginLoader Created" <<endlog(); }
PluginLoader::~PluginLoader(){ log(Debug) <<"PluginLoader Destroyed" <<endlog(); }


boost::shared_ptr<PluginLoader> PluginLoader::Instance() {
    if (!minstance)
        minstance.reset( new PluginLoader() );
    return minstance;
}

void PluginLoader::Release() {
    minstance.reset();
}

void PluginLoader::loadTypekits(string const& path_list) {
    loadPluginsInternal( path_list, "types", "typekit");
}

bool PluginLoader::loadTypekit(std::string const& name, std::string const& path_list) {
    return loadPluginInternal(name, path_list, "types", "typekit");
}

bool PluginLoader::loadPlugin(std::string const& name, std::string const& path_list) {
    return loadPluginInternal(name, path_list, "plugins", "plugin");
}

void PluginLoader::loadPlugins(string const& path_list) {
    loadPluginsInternal( path_list, "plugins", "plugin");
}

bool PluginLoader::loadService(string const& servicename, TaskContext* tc) {
    for(vector<LoadedLib>::iterator it= loadedLibs.begin(); it != loadedLibs.end(); ++it) {
        if (it->filename == servicename || it->plugname == servicename || it->shortname == servicename) {
            log(Info) << "Loading Service " << servicename << " in TaskContext " << tc->getName() <<endlog();
            return it->loadPlugin( tc );
        }
    }
    log(Error) << "No such service: "<< servicename <<endlog();
    return false;
}

void PluginLoader::loadPluginsInternal( std::string const& path_list, std::string const& subdir, std::string const& kind )
{
	// If exact match, load it directly:
    path arg( path_list );
    if (is_regular_file(arg)) {
	    loadInProcess(arg.string(), makeShortFilename(arg.filename()), kind, true);
	    return;
    }

    // prepare search path:
    vector<string> paths;
    if (path_list.empty())
    	paths = splitPaths( plugin_path);
    else
    	paths = splitPaths( path_list );

    // perform search in paths:
    for (vector<string>::iterator it = paths.begin(); it != paths.end(); ++it)
    {
        // Scan path/types/* (non recursive)
        path p = path(*it) / subdir;
        if (is_directory(p))
        {
            log(Info) << "Loading plugin libraries from directory " << p.string() << " ..."<<endlog();
            for (directory_iterator itr(p); itr != directory_iterator(); ++itr)
            {
                log(Debug) << "Scanning file " << itr->path().string() << " ...";
                if (is_regular_file(itr->status()) && itr->path().extension() == SO_EXT ) {
                    loadInProcess( itr->path().string(), makeShortFilename(itr->path().filename() ), kind, true);
                } else {
                    if (!is_regular_file(itr->status()))
                        log(Debug) << "not a regular file: ignored."<<endlog();
                    else
                        log(Debug) << "not a " + SO_EXT + " library: ignored."<<endlog();
                }
            }
        }
        else
            log(Debug) << "No such directory: " << p << endlog();

        // Repeat for types/OROCOS_TARGET:
        p = path(*it) / subdir / OROCOS_TARGET_NAME;
        if (is_directory(p))
        {
            log(Info) << "Loading plugin libraries from directory " << p.string() << " ..."<<endlog();
            for (directory_iterator itr(p); itr != directory_iterator(); ++itr)
            {
                log(Debug) << "Scanning file " << itr->path().string() << " ...";
                if (is_regular_file(itr->status()) && itr->path().extension() == SO_EXT ) {
                    loadInProcess( itr->path().string(), makeShortFilename(itr->path().filename() ), kind, true);
                } else {
                    if (!is_regular_file(itr->status()))
                        log(Debug) << "not a regular file: ignored."<<endlog();
                    else
                        log(Debug) << "not a " + SO_EXT + " library: ignored."<<endlog();
                }
            }
        }
        else
            log(Debug) << "No such directory: " << p << endlog();
    }
}

bool PluginLoader::loadPluginInternal( std::string const& name, std::string const& path_list, std::string const& subdir, std::string const& kind )
{
	// If exact match, load it directly:
    path arg( name );
    if (is_regular_file(arg)) {
	    return loadInProcess(arg.string(), makeShortFilename(arg.filename()), kind, true);
    }

    // prepare search path:
    vector<string> paths;
    if (path_list.empty())
    	paths = splitPaths( plugin_path);
    else
    	paths = splitPaths( path_list );

    vector<string> tryouts( paths.size() * 4 );
    tryouts.clear();
    if ( isLoaded(name) ) {
        log(Info) <<"Plugin '"<< name <<"' already loaded. Not reloading it." <<endlog();
        return true;
    } else {
        log(Info) << "Plugin '"<< name <<"' not loaded before." <<endlog();
    }

    path p = name;
    if (is_regular_file( p ) && loadInProcess( p.string(), name, kind, true ) )
        return true;

    for (vector<string>::iterator it = paths.begin(); it != paths.end(); ++it)
    {
        path p = path(*it) / subdir / (name + SO_EXT);
        tryouts.push_back( p.string() );
        if (is_regular_file( p ) && loadInProcess( p.string(), name, kind, true ) )
            return true;
        p = path(*it) / subdir / ("lib" + name + SO_EXT);
        tryouts.push_back( p.string() );
        if (is_regular_file( p ) && loadInProcess( p.string(), name, kind, true ) )
            return true;
        p = path(*it) / subdir / OROCOS_TARGET_NAME / (name + SO_EXT);
        tryouts.push_back( p.string() );
        if (is_regular_file( p ) && loadInProcess( p.string(), name, kind, true ) )
            return true;
        p = path(*it) / subdir / OROCOS_TARGET_NAME / ("lib" + name + SO_EXT);
        tryouts.push_back( p.string() );
        if (is_regular_file( p ) && loadInProcess( p.string(), name, kind, true ) )
            return true;
    }
    log(Error) << "No such "<< kind << " found in path: " << name << ". Tried:"<< endlog();
    for(vector<string>::iterator it=tryouts.begin(); it != tryouts.end(); ++it)
        log(Error) << *it << " ";
    log(Error)<< endlog();
    return false;
}

bool PluginLoader::isLoaded(string file)
{
    path p(file);
    std::vector<LoadedLib>::iterator lib = loadedLibs.begin();
    while (lib != loadedLibs.end()) {
        // there is already a library with the same name
        if ( lib->filename == p.filename() || lib->plugname == file || lib->shortname == file ) {
            return true;
        }
        ++lib;
    }
    return false;
}

// loads a single plugin in the current process.
bool PluginLoader::loadInProcess(string file, string shortname, string kind, bool log_error) {
    path p(file);
    char* error;
    void* handle;

    if ( isLoaded(shortname) || isLoaded(file) ) {
        if (log_error)
            log(Warning);
        else
            log(Debug);
        log() <<"plugin '"<< file <<"' already loaded. Not reloading it." <<endlog() ;
        return false;
    }

    handle = dlopen ( p.string().c_str(), RTLD_NOW | RTLD_GLOBAL );

    if (!handle) {
        string e( dlerror() );
        if (log_error)
            log(Error) << "could not load library '"<< p.string() <<"': "<< e <<endlog();
        else
            endlog();
        return false;
    }

    //------------- if you get here, the library has been loaded -------------
    string libname = p.filename();
    log(Debug)<<"Found library "<<libname<<endlog();
    LoadedLib loading_lib(libname,shortname,handle);
    dlerror();    /* Clear any existing error */

    std::string(*pluginName)(void) = 0;
    std::string(*targetName)(void) = 0;
    loading_lib.loadPlugin = (bool(*)(RTT::TaskContext*))(dlsym(handle, "loadRTTPlugin") );
    if ((error = dlerror()) == NULL) {
        string plugname, targetname;
        pluginName = (std::string(*)(void))(dlsym(handle, "getRTTPluginName") );
        if ((error = dlerror()) == NULL) {
            plugname = (*pluginName)();
        } else {
            plugname  = libname;
        }
        loading_lib.plugname = plugname;
        targetName = (std::string(*)(void))(dlsym(handle, "getRTTTargetName") );
        if ((error = dlerror()) == NULL) {
            targetname = (*targetName)();
        } else {
            targetname  = OROCOS_TARGET_NAME;
        }
        if ( targetname != OROCOS_TARGET_NAME ) {
            log(Error) << "Plugin "<< plugname <<" reports to be compiled for OROCOS_TARGET "<< targetname
                    << " while we are running on target "<< OROCOS_TARGET_NAME <<". Unloading."<<endlog();
            dlclose(handle);
            return false;
        }

        // ok; try to load it.
        bool success = false;
        try {
            // Load into process (TaskContext* == 0):
            success = (*loading_lib.loadPlugin)( 0 );
        } catch(...) {
            log(Error) << "Unexpected exception in loadRTTPlugin !"<<endlog();
        }

        if ( !success ) {
            log(Error) << "Failed to load RTT Plugin '" <<plugname<<"': plugin refused to load into this process. Unloading." <<endlog();
            dlclose(handle);
            return false;
        }
        if (kind == "typekit") {
            log(Info) << "Loaded RTT TypeKit/Transport '" + plugname + "' from '" + shortname +"'"<<endlog();
            loading_lib.is_typekit = true;
        } else {
            log(Info) << "Loaded RTT Plugin '" + plugname + "' from '" + shortname +"'"<<endlog();
            loading_lib.is_typekit = false;
        }
        loadedLibs.push_back(loading_lib);
        return true;
    } else {
        if (log_error)
            log(Error) <<"Not a plugin: " << error << endlog();
    }
    dlclose(handle);
    return false;
}

std::vector<std::string> PluginLoader::listServices() const {
    vector<string> names;
    for(vector<LoadedLib>::const_iterator it= loadedLibs.begin(); it != loadedLibs.end(); ++it) {
        if ( !it->is_typekit )
            names.push_back( it->plugname );
    }
    return names;
}

std::vector<std::string> PluginLoader::listPlugins() const {
    vector<string> names;
    for(vector<LoadedLib>::const_iterator it= loadedLibs.begin(); it != loadedLibs.end(); ++it) {
        names.push_back( it->plugname );
    }
    return names;
}

std::vector<std::string> PluginLoader::listTypekits() const {
    vector<string> names;
    for(vector<LoadedLib>::const_iterator it= loadedLibs.begin(); it != loadedLibs.end(); ++it) {
        if ( it->is_typekit )
            names.push_back( it->plugname );
    }
    return names;
}

std::string PluginLoader::getPluginPath() const {
    return plugin_path;
}

void PluginLoader::setPluginPath( std::string const& newpath ) {
    plugin_path = newpath;
}
