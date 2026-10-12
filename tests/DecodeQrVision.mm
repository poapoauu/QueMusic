#include <QImage>
#include <QString>
#import <Vision/Vision.h>

// Test-only independent platform decoder; never linked into Host/Plugin UI.
QString decodeQrVision(const QImage &input)
{
    @autoreleasepool {
        const QImage image = input.convertToFormat(QImage::Format_RGBA8888);
        CGDataProviderRef provider = CGDataProviderCreateWithData(nullptr, image.constBits(), image.sizeInBytes(), nullptr);
        CGColorSpaceRef colour = CGColorSpaceCreateDeviceRGB();
        CGImageRef cgImage = CGImageCreate(image.width(), image.height(), 8, 32, image.bytesPerLine(),
            colour, kCGImageAlphaLast | kCGBitmapByteOrderDefault, provider, nullptr, false, kCGRenderingIntentDefault);
        VNDetectBarcodesRequest *request = [[VNDetectBarcodesRequest alloc] init];
        request.symbologies = @[ VNBarcodeSymbologyQR ];
        VNImageRequestHandler *handler = [[VNImageRequestHandler alloc] initWithCGImage:cgImage options:@{}];
        NSError *error = nil;
        QString result;
        if ([handler performRequests:@[request] error:&error] && request.results.count == 1) {
            VNBarcodeObservation *observation = request.results.firstObject;
            if (observation.payloadStringValue) result = QString::fromUtf8(observation.payloadStringValue.UTF8String);
        }
        [handler release]; [request release];
        CGImageRelease(cgImage); CGColorSpaceRelease(colour); CGDataProviderRelease(provider);
        return result;
    }
}
