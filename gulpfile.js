var Fs = require('fire-fs');
var Path = require('path');
var gulp = require('gulp');
var Utils = require('./script/fireball/utils');
var gulpSequence = require('gulp-sequence');
var AdmZip = require('adm-zip');
var Del = require('del');

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

gulp.task('update', function(cb) {
    var execCmd = 'python ' + Path.join('script', 'update.py');
    Utils.execSync(execCmd, '.');
    cb();    
});

gulp.task('bootstrap', function(cb) {
    Utils.execSync('python script/bootstrap.py -v --target_arch=x64');
    cb();
});

gulp.task('show-download-url', function(cb) {
    Utils.execSync('python script/show-url.py');
    cb();
});
 
gulp.task('create-dist', function(cb) {
    var execCmd = 'python ' + Path.join('script', 'create-dist.py');
    Utils.execSync(execCmd, '.');
    cb();
});

gulp.task('sync-submodule', function(cb) {
    Utils.execSync('git submodule sync', '.');
    cb();
});

gulp.task('update-submodule', function(cb) {
    Utils.execSync('git submodule update --init --recursive', '.');
    cb();
});

gulp.task('extract-libchromiumcontent', function(cb) {
    var srcDynamic = Path.join('download', 'libchromiumcontent.zip');
    var srcStatic = Path.join('download', 'libchromiumcontent-static.zip');
    if (Fs.existsSync(srcDynamic) === false ||
        Fs.existsSync(srcStatic) === false) {
        console.log("Please make sure you have downloaded libchromiumcontent from AWS and put it to ./download folder.");
        process.exit(1);
        return;
    }
    var destPath = 'vendor/brightray/vendor/download/libchromiumcontent';
    Del(destPath).then(function() {
        console.log('cleaned target folder: ' + destPath);
        Fs.ensureDirSync(destPath);
        var zipDynamic = new AdmZip(srcDynamic);
        zipDynamic.extractAllTo(destPath, true);
        var zipStatic = new AdmZip(srcStatic);
        zipStatic.extractAllTo(destPath, true);
        Fs.writeFileSync(Path.join(destPath, '.version'), '', {encoding:'utf8'});
        cb();
    });
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

gulp.task('auto-dist', gulpSequence('update', 'create-dist', 'upload-dist-ftp'));

gulp.task('init-with-external-download', gulpSequence('sync-submodule', 'update-submodule', 'extract-libchromiumcontent', 'bootstrap'));
