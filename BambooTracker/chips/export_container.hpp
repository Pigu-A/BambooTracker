#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace chip
{
	class ExportContainerInterface
	{
	public:
		virtual ~ExportContainerInterface();
		virtual void recordRegisterChange(uint32_t offset, uint8_t value) = 0;
		virtual void recordStream(int16_t* stream, size_t nSamples) = 0;
		virtual bool empty() const = 0;
		virtual void clear() = 0;
	};

	class WavExportContainer : public ExportContainerInterface
	{
	public:
		WavExportContainer();
		void recordRegisterChange(uint32_t offset, uint8_t value) override;
		void recordStream(int16_t* stream, size_t nSamples) override;
		bool empty() const override;
		void clear() override;
		std::vector<int16_t> getStream() const;

	private:
		std::vector<int16_t> samples_;
	};

	class VgmExportContainer : public ExportContainerInterface
	{
	public:
		VgmExportContainer(uint32_t intrRate);
		void recordRegisterChange(uint32_t offset, uint8_t value) override;
		void recordStream(int16_t* stream, size_t nSamples) override;
		void clear() override;
		bool empty() const override;
		std::vector<uint8_t> getData();
		size_t getSampleLength() const;
		size_t setLoopPoint();
		size_t forceMoveLoopPoint();

	private:
		std::vector<uint8_t> buf_;
		uint64_t lastWait_, totalSampCnt_;
		uint32_t intrRate_;
		bool isSetLoop_;
		uint32_t loopPoint_;

		void setWait();
	};

	class S98ExportContainer : public ExportContainerInterface
	{
	public:
		S98ExportContainer();
		void recordRegisterChange(uint32_t offset, uint8_t value) override;
		void recordStream(int16_t* stream, size_t nSamples) override;
		void clear() override;
		bool empty() const override;
		std::vector<uint8_t> getData();
		size_t getSampleLength() const;
		size_t setLoopPoint();
		size_t forceMoveLoopPoint();

	private:
		std::vector<uint8_t> buf_;
		uint64_t lastWait_, totalSampCnt_;
		bool isSetLoop_;
		uint32_t loopPoint_;

		void setWait();
	};
}
