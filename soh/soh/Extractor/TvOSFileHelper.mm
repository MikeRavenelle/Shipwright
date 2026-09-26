#ifdef __TVOS__

#import <Foundation/Foundation.h>
#include "TvOSFileHelper.h"
#include <spdlog/spdlog.h>

bool TvOSWriteFile(const std::string& destDir, const std::string& filename,
                   const char* data, std::size_t size) {
    @autoreleasepool {
        NSError* dirErr = nil;
        NSURL* cachesURL =
            [[NSFileManager defaultManager]
                URLForDirectory:NSCachesDirectory
                       inDomain:NSUserDomainMask
              appropriateForURL:nil
                         create:YES
                          error:&dirErr];
        if (!cachesURL) {
            SPDLOG_ERROR("[TvOSUpload] Cannot resolve NSCachesDirectory: {}",
                         dirErr ? dirErr.localizedDescription.UTF8String : "unknown");
            return false;
        }

        NSString* nsFname = [NSString stringWithUTF8String:filename.c_str()];
        NSURL*    destURL = [cachesURL URLByAppendingPathComponent:nsFname];

        NSData* nsData = [NSData dataWithBytesNoCopy:(void*)data length:size freeWhenDone:NO];
        NSError* writeErr = nil;
        BOOL ok = [nsData writeToURL:destURL
                             options:NSDataWritingAtomic
                               error:&writeErr];
        if (!ok) {
            SPDLOG_ERROR("[TvOSUpload] writeToURL {} failed: {}",
                         destURL.path.UTF8String,
                         writeErr ? writeErr.localizedDescription.UTF8String : "unknown");
            return false;
        }

        SPDLOG_INFO("[TvOSUpload] Wrote {} bytes to {}", size, destURL.path.UTF8String);
        return true;
    }
}

#endif
