var Path = require('path');
var Fs = require('fs');
var Ftp = require('ftp');
var ExecSync = require('child_process').execSync;

function execSync(cmd, workPath) {
    var execOptions = {
        cwd : workPath,
        stdio : 'inherit'
    };
    ExecSync(cmd, execOptions);
}

function upload2Ftp(localPath, ftpPath, config, cb) {
    var ftpClient = new Ftp();
    ftpClient.on('error', function(err) {
        if (err) {
            cb(err);
        }
    });
    ftpClient.on('ready', function() {
        var dirName = Path.dirname(ftpPath);
        ftpClient.mkdir(dirName, true, function(err) {
            if (err) {
                cb(err);
            }
        });

        ftpClient.put(localPath, ftpPath, function(err) {
            if (err) {
                cb(err);
            }
            ftpClient.end();
            cb();
        });
    });

    // connect to ftp
    ftpClient.connect(config);
}

module.exports = {
    execSync: execSync,
    upload2Ftp: upload2Ftp
};