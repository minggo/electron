var Fs = require('fs');
var Path = require('path');
var gulp = require('gulp');
var Utils = require('./script/fireball/utils');
var gulpSequence = require('gulp-sequence');

gulp.task('build-debug', function(cb) {
    var execCmd = 'python ' + Path.join('script', 'build.py') + ' -c D';
    Utils.execSync(execCmd, '.');
    cb();
});

gulp.task('build-release', function(cb) {
    var execCmd = 'python ' + Path.join('script', 'build.py') + ' -c R';
    Utils.execSync(execCmd, '.');
    cb();
});

gulp.task('create-dist', function(cb) {
    var execCmd = 'python ' + Path.join('script', 'create-dist.py');
    Utils.execSync(execCmd, '.');
    cb();
});

gulp.task('upload-dist-ftp', function(cb) {
    var version = Fs.readFileSync('dist/version', {encoding:'utf8'});
    var platform = process.platform;
    var arch = platform === 'win32' ? 'ia32' : 'x64';
    var zipFileName = 'electron-' + version + '-' + platform + '-' + arch + '.zip';
    var zipFilePath = Path.join('dist', zipFileName);
    // upload to ftp
    var remotePath = Path.join('TestBuilds','Fireball', 'Electron', version, zipFileName);
    Utils.upload2Ftp(zipFilePath, remotePath, {
        host: '192.168.52.109',
        user: 'xmdev',
        password: 'chukongxm'
    }, function(err) {
        if (err) {
            throw err;
        }
        cb();
    });
});

gulp.task('auto-dist', gulpSequence('create-dist', 'upload-dist-ftp'));