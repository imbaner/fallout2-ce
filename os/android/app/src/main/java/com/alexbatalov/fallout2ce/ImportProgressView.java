package com.alexbatalov.fallout2ce;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.os.SystemClock;
import android.util.AttributeSet;
import android.view.View;

// The import screen's progress in the mobile UI's look (the system's bar
// doesn't fit it): a track with a green fill, or, while the amount isn't
// known (looking for the game), a green segment going back and forth.
public class ImportProgressView extends View {
    private static final long SWEEP_MS = 1400;

    private final Paint track = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint border = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF rect = new RectF();

    private boolean indeterminate;
    private float fraction;

    public ImportProgressView(Context context, AttributeSet attrs) {
        super(context, attrs);
        track.setColor(context.getColor(R.color.import_button));
        border.setColor(context.getColor(R.color.import_button_border));
        border.setStyle(Paint.Style.STROKE);
        border.setStrokeWidth(getResources().getDisplayMetrics().density);
        fill.setColor(context.getColor(R.color.import_button_primary));
    }

    public void setIndeterminate(boolean indeterminate) {
        this.indeterminate = indeterminate;
        invalidate();
    }

    // [fraction] of the work done, 0..1.
    public void setFraction(float fraction) {
        this.fraction = Math.max(0.0f, Math.min(fraction, 1.0f));
        invalidate();
    }

    @Override
    protected void onDraw(Canvas canvas) {
        float inset = border.getStrokeWidth() / 2.0f;
        float radius = getHeight() / 2.0f;
        rect.set(inset, inset, getWidth() - inset, getHeight() - inset);
        canvas.drawRoundRect(rect, radius, radius, track);

        float inner = border.getStrokeWidth() * 2.0f;
        float width = getWidth() - inner * 2.0f;
        float left;
        float right;
        if (indeterminate) {
            // A third of the track, eased from one end to the other and back.
            float segment = width / 3.0f;
            float t = (SystemClock.uptimeMillis() % (SWEEP_MS * 2)) / (float) SWEEP_MS;
            float phase = t <= 1.0f ? t : 2.0f - t;
            float eased = phase * phase * (3.0f - 2.0f * phase);
            left = inner + (width - segment) * eased;
            right = left + segment;
            postInvalidateOnAnimation();
        } else {
            left = inner;
            right = inner + width * fraction;
        }

        if (right > left) {
            rect.set(left, inner, right, getHeight() - inner);
            float fillRadius = (getHeight() - inner * 2.0f) / 2.0f;
            canvas.drawRoundRect(rect, fillRadius, fillRadius, fill);
        }

        rect.set(inset, inset, getWidth() - inset, getHeight() - inset);
        canvas.drawRoundRect(rect, radius, radius, border);
    }
}
